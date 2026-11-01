"""Build-only resource compiler: strict JSON, matching keys and format parameters."""
import argparse
import json
import pathlib
import re


def unique_object(pairs):
    value = {}
    for key, entry in pairs:
        if key in value:
            raise ValueError(f"Duplicate JSON key: {key}")
        value[key] = entry
    return value


def validate(directory):
    languages = []
    for path in sorted(pathlib.Path(directory).glob("*.json")):
        language = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
        if set(language) != {"name", "locale", "translations"}:
            raise ValueError(f"Invalid language schema: {path}")
        if not isinstance(language["name"], str) or not language["name"].strip():
            raise ValueError(f"Missing native language name: {path}")
        if not re.fullmatch(r"[a-z]{2}-[A-Z]{2}", language["locale"]):
            raise ValueError(f"Invalid locale: {path}")
        if not isinstance(language["translations"], dict) or not language["translations"]:
            raise ValueError(f"Missing translations: {path}")
        for key, translation in language["translations"].items():
            if not re.fullmatch(r"[a-z][a-zA-Z0-9_.]*", key):
                raise ValueError(f"Invalid translation key: {key}")
            variants = [translation]
            if isinstance(translation, dict):
                if set(translation) != {"one", "other"}:
                    raise ValueError(f"Plural must have one/other variants: {key}")
                variants = translation.values()
            for text in variants:
                if not isinstance(text, str) or not text.strip() or "\x00" in text:
                    raise ValueError(f"Empty/invalid translation: {key}")
                if re.search(r"[{}]", re.sub(r"\{[a-zA-Z_][a-zA-Z_0-9]*\}", "", text)):
                    raise ValueError(f"Invalid placeholders in {key}: {text}")
        languages.append(language)
    if len({entry["locale"] for entry in languages}) != len(languages):
        raise ValueError("Duplicate locale")
    if {entry["locale"] for entry in languages} != {"en-US", "pt-BR", "es-ES"}:
        raise ValueError("Exactly en-US, pt-BR and es-ES must be embedded")
    english = next(entry["translations"] for entry in languages if entry["locale"] == "en-US")
    def structure(entry):
        if isinstance(entry, dict):
            return {key: structure(text) for key, text in entry.items()}
        return sorted(re.findall(r"\{([a-zA-Z_][a-zA-Z_0-9]*)\}", entry))
    for entry in languages:
        translations = entry["translations"]
        if set(translations) != set(english):
            raise ValueError(f"Translation keys differ in {entry['locale']}")
        for key in english:
            if structure(translations[key]) != structure(english[key]):
                raise ValueError(f"Placeholder/plural mismatch: {entry['locale']} {key}")
    return languages


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    parser.add_argument("output", nargs="?")
    options = parser.parse_args()
    languages = validate(options.directory)
    if options.output:
        encoded = json.dumps(languages, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        source = '#include <string_view>\nnamespace aegis::embedded {\n'
        source += 'std::string_view languages() noexcept {\nstatic constexpr char data[] = {'
        source += ",".join(str(byte) for byte in encoded) + ',0};\nreturn {data,sizeof(data)-1};\n}\n}\n'
        output = pathlib.Path(options.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(source, encoding="utf-8")
    print(f"Validated {len(languages)} embedded languages")
