#!/usr/bin/env python3
"""Owned-guest isolation/consistency tests. A result is never silently suppressed."""
from pathlib import Path
import argparse
import json
import os
import random
import secrets
import subprocess
import sys
import time

REVISION = {'schema': 1, 'revision': 1, 'seed': 42, 'templateSha256': 'ffe3d0ff593b0ff7b3dbc44ddaefd8fe943fd953e2df16af85149c14c26bbea5', 'purpose': 'isolation-and-consistency-regression', 'kernelModified': False, 'undetectabilityClaimed': False}


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def inspect_report(data, case, canary):
    check(data.get("finished"), "Coleta não finalizada")
    check(canary not in json.dumps(data), "Dado privado do host presente na coleta")
    if case == "overflow":
        check(not data["complete"] and data["dropped"] > 0, "Perda de eventos foi ocultada")
        check(any(f["classification"] == "inconclusive" for f in data["findings"]),
              "Coleta incompleta declarada conclusiva")
        return
    check(data["complete"] and not any(data[k] for k in ("dropped", "rejected", "pending")),
          "Coleta incompleta ou inconsistente")
    events = data["events"]
    check([e["sequence"] for e in events] == list(range(1, len(events) + 1)), "Sequência divergente")
    pending = {}
    for e in events:
        if e["stage"] == "intent":
            check(e["operationId"] not in pending, "Operação duplicada")
            pending[e["operationId"]] = e
        else:
            before = pending.pop(e["operationId"], None)
            check(before is not None, "Resultado sem intenção")
            check(all(e[k] == before[k] for k in ("pid", "tid", "actorPid", "actorTid", "domain",
                                                "operation", "resource", "decision")), "Autoria divergente")
            if e["decision"] == "deny":
                check(e["status"] != 0, "Recusa declarada como sucesso")
    check(not pending, "Operação sem resultado")
    bulk = any(f["rule"] == "bulk-rewrite-rename-v1" for f in data["findings"])
    check(bulk == (case in ("patterns", "simulated", "offline")), "Classificação incoerente")
    if case == "benign":
        check(any(f["classification"] == "legitimate" for f in data["findings"]),
              "Programa benigno perdeu sua classificação")
    if case == "denied":
        writes = [e for e in events if e["stage"] == "result" and e["domain"] == "file"
                  and e["operation"] == "write" and e["resource"].lower() == "c:\\a.txt"]
        check(writes and all(e["decision"] == "deny" and e["status"] != 0
                              and e["oldHash"] == e["newHash"]
                              and e["beforeBytes"] == e["afterBytes"] for e in writes),
              "Arquivo recusado foi alterado ou o erro foi ocultado")
    if case == "simulated":
        check(any(e["domain"] == "file" and e["stage"] == "result" and e["operation"] == "write"
                  and e["decision"] == "simulate" and e["status"] == 0
                  and e["oldHash"] != e["newHash"] for e in events), "Simulação não produziu estado coerente")
    domains = {e["domain"] for e in events}
    if case in ("patterns", "simulated"):
        check({"process", "thread", "module", "file", "registry", "service", "driver", "network", "memory"}
              <= domains, "Domínios ausentes")


def run_suite(manager, kernel, output):
    output = output.resolve()
    # Refuse an old evidence directory: never replace a failure with a later success.
    output.mkdir(parents=True, exist_ok=False)
    catalog = output / "catalog"
    proof = {"status": "RUNNING", "revision": REVISION, "wine": False, "scenarios": []}
    commands = []
    canary = "Aegis-private-" + secrets.token_hex(24)
    private = output / "host-private-control.txt"
    private.write_text(canary, encoding="utf-8")
    env = {**os.environ, "GUARDIAN_TEST_PRIVATE_TOKEN": canary}

    def run(*args):
        p = subprocess.run([str(manager), "--root", str(catalog), *map(str, args)],
                           capture_output=True, text=True, encoding="utf-8", errors="replace",
                           env=env, timeout=45)
        commands.append({"args": list(map(str, args)), "exit": p.returncode})
        check(p.returncode == 0, p.stderr or p.stdout or "Gerenciador falhou sem diagnóstico")
        return json.loads(p.stdout)

    names = ["patterns", "denied", "simulated", "benign", "overflow", "offline"]
    random.Random(REVISION["seed"]).shuffle(names)
    active = []
    try:
        for name in names:
            run("create", name, kernel, "storage", "--host-reserve-mib", "1024")
            active.append(name)
            if name != "offline":
                run("network", name, "simulated")
            decision = {"denied": "deny", "simulated": "simulate"}.get(name, "allow")
            extra = ["--scenario", "benign"] if name == "benign" else []
            if name == "overflow":
                extra += ["--event-limit", "16"]
            run("analysis-policy", name, "file", decision, *extra)
            run("start", name)
            deadline = time.monotonic() + 40
            while True:
                status = run("status", name)
                if not status["resident"] and status["state"] in ("stopped", "fault"):
                    break
                check(time.monotonic() < deadline, "VM excedeu o tempo limite")
                time.sleep(.1)
            check(status["state"] == ("fault" if name == "overflow" else "stopped"), "Estado final inesperado")
            trace = run("analysis", name)
            inspect_report(trace, name, canary)
            run("analysis-export", name, output / (name + "-report"))
            exported = output / (name + "-report")
            check(json.loads((exported / "analysis.json").read_text(encoding="utf-8")) == trace,
                  "Exportação difere do estado real")
            html = (exported / "analysis.html").read_text(encoding="utf-8")
            check("Content-Security-Policy" in html and "<script" not in html.lower(), "HTML ativo/sem proteção")
            check(private.read_text(encoding="utf-8") == canary, "Arquivo do host foi alterado")
            proof["scenarios"].append({"case": name, "status": "PASS", "events": len(trace["events"])})
        proof.update(status="PASS", kernelModified=False, undetectabilityClaimed=False,
                     isolatedGuestEffects=True, emptyResponsesUsed=False)
    except Exception as e:
        proof.update(status="FAIL", error=str(e))
        raise
    finally:
        cleanup_errors = []
        for name in active:
            try:
                if run("status", name).get("resident"):
                    run("shutdown", name)
            except Exception as e:
                cleanup_errors.append(str(e))
        if cleanup_errors:
            proof.update(status="FAIL", cleanupErrors=cleanup_errors)
        (output / "result.json").write_text(json.dumps(proof, indent=2) + "\n", encoding="utf-8")
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")
    check(proof["status"] == "PASS", "Falha na limpeza; veja result.json")
    return proof


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manager", type=Path, required=True)
    p.add_argument("--kernel", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    if REVISION is None:
        p.error("Gere uma revisão com tools/aegis_sandbox_revisions.py generate")
    result = run_suite(a.manager.resolve(), a.kernel.resolve(), a.output)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(json.dumps({"status": "FAIL", "error": str(error)}), file=sys.stderr)
        sys.exit(1)
