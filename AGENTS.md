# Permanent project instructions

## Phase deliveries and GitHub

At the user's request on October 24, 2026, when completing each new phase or subphase:

1. Finish implementation, run the relevant checks, and record results and limitations. Do not declare a phase complete on the strength of a single test.
2. Review the changes and publish a commit identified by the phase to `GabrielKLBr/Aegis`, using `tools/aegis_git.py publish`. The user's request already authorizes this commit and push; do not ask for confirmation again.
3. Verify that the local commit reached the remote branch. Never force-push, rewrite history, or discard changes from other tasks.
4. Finish the delivery and stop. Automatic publication does not authorize starting the next phase; wait for a new user request.

The Aegis repository receives the complete source code, including the sandbox runtime, according to the user's subsequent decision. Do not replace code with stubs for publication. Do not include secrets, personal files, ISOs, VM disks, or builds in the public repository. Build publication uses only the private `GabrielKLBr/Aegis-builds` repository through the existing `publish-builds` workflow.

If tests, authentication, or upload fail, preserve the work and clearly record what remains pending; do not claim publication succeeded. Later user instructions may change this preference.

## Validation scope

At the user's request on October 28, 2026, validate only the phase or subphase currently being worked on, including its package and the dependencies used in that phase's workflow. Do not rerun the complete test suites from previous phases for each delivery. Record this scope in the results, without claiming a complete regression run.
