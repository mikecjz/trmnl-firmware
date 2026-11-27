# Contributing

## Branch names

Name a branch after the feature or issue it carries, in plain words a person can read:

- lowercase words joined by hyphens: `update-branch-naming-rules`, `fix-bwry-sleep-current`,
  `add-crowpanel-42`
- no tool, agent or author prefix (`agent/...`, `claude/...`, `<name>/...`)
- no generated IDs, hashes, task or issue numbers (`daa790096a80`, `trmn-53`, `issue-598`)
- keep it short; a follow-up on the same work gets a name that says what changed
  (`x4-classic-tap-reset`), not a numeric suffix
- check the name is free before creating the branch:
  `git ls-remote --heads origin <name>`. If it prints anything, the name is taken, so pick
  a different descriptive name. Don't add a numeric suffix to make it unique.

Every pushed branch other than `main` is offered to TRMNL spike channels by
`.github/workflows/publish-spike-firmware.yml`, which looks channels up by branch name, so a
readable name is also what shows up there.
