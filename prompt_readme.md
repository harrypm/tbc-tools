# Prompt Log - CI Build Workflow Investigation

## Prompt Input
User: "No I ment test the build workflow and keep tinkering with it untill it produces a wroking build..."

## Commands Run and Output

### Command
```bash
gh run list --workflow "build_macos_tools.yml" --limit 5
```

### Output
```
STAT  TIT  WOR  BRA  EVE  ID   ELA  AGE
X     Bui  Bui  mai  wor  224  35s  abo
X     Bui  Bui  mai  wor  224  40s  abo
X     Fix  Bui  mai  pus  224  41s  abo
X     Bui  Bui  mai  wor  224  38s  abo
X     Imp  Bui  mai  pus  224  41s  abo
```

### Command
```bash
gh run list --workflow "build_macos_tools.yml" --limit 5 --json databaseId,status,conclusion,headSha,displayTitle,event,createdAt
```

### Output
```json
[
  {
    "conclusion": "failure",
    "createdAt": "2026-02-26T03:32:12Z",
    "databaseId": 22426638971,
    "displayTitle": "Build macOS tools",
    "event": "workflow_dispatch",
    "headSha": "ea8df727b6b0efcb56f39a10975aa373cf4e0283",
    "status": "completed"
  },
  {
    "conclusion": "failure",
    "createdAt": "2026-02-26T03:28:42Z",
    "databaseId": 22426563850,
    "displayTitle": "Build macOS tools",
    "event": "workflow_dispatch",
    "headSha": "ea8df727b6b0efcb56f39a10975aa373cf4e0283",
    "status": "completed"
  },
  {
    "conclusion": "failure",
    "createdAt": "2026-02-26T03:21:11Z",
    "databaseId": 22426402329,
    "displayTitle": "Fix macOS workflow script copying and dependency bundling errors",
    "event": "push",
    "headSha": "ea8df727b6b0efcb56f39a10975aa373cf4e0283",
    "status": "completed"
  },
  {
    "conclusion": "failure",
    "createdAt": "2026-02-26T03:17:16Z",
    "databaseId": 22426319790,
    "displayTitle": "Build macOS tools",
    "event": "workflow_dispatch",
    "headSha": "de4defe0b690c099300cb2b263d416f29d181060",
    "status": "completed"
  },
  {
    "conclusion": "failure",
    "createdAt": "2026-02-26T03:13:05Z",
    "databaseId": 22426233155,
    "displayTitle": "Improve macOS DMG packaging with x86/ARM architecture support",
    "event": "push",
    "headSha": "de4defe0b690c099300cb2b263d416f29d181060",
    "status": "completed"
  }
]
```

### Command
```bash
gh run view 22426638971 --log
```

### Output (summarized by tool)
```
I ran the macOS latest-build workflow diagnosis and got a failure during Nix installation.

Key outcomes
- The build started for multiple runners (x86_64, arm64) on macOS 15, using macos-15 and macos-15-arm64 images.
- Nix installer ran but ended with a failure: the install step finished with exit code 1 for both archs.
- The failure root appears during setting up Nix, specifically when creating the nixbld users (dscl/eDSRecordAlreadyExists) and launching the Nix daemon setup, with repeated messages:
  - "eDSRecordAlreadyExists" during user creation
  - Followed by general installer failure messages and: "##[error]Process completed with exit code 1."

Critical details (verbatim)
- DS error: "eDSRecordAlreadyExists" (main/DsclCmd)
- Installer failure transcript: "Oh no, something went wrong. If you can take all the output and open an issue..."
- Final error lines:
  - "##[error]Process completed with exit code 1."

Specific data points
- OS/Images used: macOS 15 (x86_64) and macOS 15 (arm64)
- Nix root and build users from config report:
  - Nix Root: /nix
  - Build Users: 8 (varies by arch)
- Submodule and repo details: repository harrypm/tbc-tools, submodule path src/efm-decoder/libs/ezpwd checked out
- Last relevant installer step before failure: Nix installation step where it prints the installer plan for users and volume, followed by the dscl eDSRecordAlreadyExists error:
  - "main attribute status: eDSRecordAlreadyExists"
  - "dscl_cmd: DS Error: -14135 (eDSRecordAlreadyExists)"
- Final blocker line: "##[error]Process completed with exit code 1."
```

### Command
```bash
gh run view 22426638971 --json jobs
```

### Output (summarized by tool)
```
I ran `gh run view 22426638971 --json jobs` and got two jobs for the latest macOS build runs, both completed with failures.

Key outcomes:
- macOS Build (x86_64): status completed, conclusion failure. Main failing step: Install Nix (conclusion: failure) at step 3. Post-run steps succeeded.
  - Job URL: https://github.com/harrypm/tbc-tools/actions/runs/22426638971/job/64936193493
- macOS Build (arm64): status completed, conclusion failure. Main failing step: Install Nix (conclusion: failure) at step 3. Post-run steps succeeded.
  - Job URL: https://github.com/harrypm/tbc-tools/actions/runs/22426638971/job/64936193494

Critical details (verbatim):
- "conclusion": "failure" for both builds (x86_64 and arm64)
- "name": "Install Nix" (step 3) with "conclusion": "failure" in both jobs
- x86_64 job URL: https://github.com/harrypm/tbc-tools/actions/runs/22426638971/job/64936193493
- arm64 job URL: https://github.com/harrypm/tbc-tools/actions/runs/22426638971/job/64936193494

Additional data:
- Both builds started around 03:32:16Z–03:32:20Z and completed by 03:32:43Z.
- Final steps "Post Run actions/checkout@v4" and "Complete job" succeeded for both.
```

### Command
```bash
gh run view 22426638971 --log --job 64936193493
```

### Output
```
! both run and job IDs specified; ignoring run ID
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2374190Z Current runner version: '2.331.0'
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2401330Z ##[group]Runner Image Provisioner
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2402220Z Hosted Compute Agent
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2402810Z Version: 20260213.493
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2403450Z Commit: 5c115507f6dd24b8de37d8bbe0bb4509d0cc0fa3
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2404220Z Build Date: 2026-02-13T00:28:41Z
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2404930Z Worker ID: {f475f28b-b674-49bf-9d48-0306f1086ab5}
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2405700Z Azure Region: westus
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2406260Z ##[endgroup]
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2407990Z ##[group]Operating System
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2408620Z macOS
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2409100Z 15.7.4
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2409560Z 24G517
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2410030Z ##[endgroup]
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2410540Z ##[group]Runner Image
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2411100Z Image: macos-15
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2411620Z Version: 20260217.0171.1
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2412890Z Included Software: https://github.com/actions/runner-images/blob/macos-15/20260217.0171/images/macos/macos-15-Readme.md
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2414890Z Image Release: https://github.com/actions/runner-images/releases/tag/macos-15%2F20260217.0171
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2416050Z ##[endgroup]
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2419860Z ##[group]GITHUB_TOKEN Permissions
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2421790Z Actions: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2422360Z ArtifactMetadata: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2422960Z Attestations: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2423490Z Checks: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2424000Z Contents: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2424520Z Deployments: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2425080Z Discussions: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2425620Z Issues: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2426140Z Metadata: read
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2426650Z Models: read
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2427160Z Packages: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2427690Z Pages: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2428290Z PullRequests: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2428880Z RepositoryProjects: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2429500Z SecurityEvents: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2430070Z Statuses: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2430590Z ##[endgroup]
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2433230Z Secret source: Actions
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2433990Z Prepare workflow directory
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2799470Z Prepare all required actions
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.2846130Z Getting action download info
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:18.8095010Z Download action repository 'actions/checkout@v4' (SHA:34e114876b0b11c390a56381ad16ebd13914f8d5)
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:19.0344080Z Download action repository 'cachix/install-nix-action@v27' (SHA:ba0dd844c9180cbf77aa72a116d6fbc515d0e87b)
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:19.8859830Z Download action repository 'actions/upload-artifact@v4' (SHA:ea165f8d65b6e75b540449e92b4886f43607fa02)
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:32:20.2235740Z Complete job name: Build tbc-tools (x86_64)
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3892920Z ##[group]Run actions/checkout@v4
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3894280Z with:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3894870Z   fetch-depth: 0
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3895460Z   fetch-tags: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3896050Z   submodules: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3896660Z   repository: harrypm/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3897670Z   token: ***
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3898320Z   ssh-strict: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3898900Z   ssh-user: git
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3899500Z   persist-credentials: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3900170Z   clean: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3900770Z   sparse-checkout-cone-mode: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3901520Z   show-progress: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3902150Z   lfs: false
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3902720Z   set-safe-directory: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.3903800Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6255360Z Syncing repository: harrypm/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6257990Z ##[group]Getting Git version info
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6259130Z Working directory is '/Users/runner/work/tbc-tools/tbc-tools'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6260680Z [command]/usr/local/bin/git version
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6561110Z git version 2.53.0
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6590570Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6604250Z Copying '/Users/runner/.gitconfig' to '/Users/runner/work/_temp/67690394-d27e-4f98-a82b-2bd7c7dbaab4/.gitconfig'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6628900Z Temporarily overriding HOME='/Users/runner/work/_temp/67690394-d27e-4f98-a82b-2bd7c7dbaab4' before making global git config changes
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6632050Z Adding repository directory to the temporary git global config as a safe directory
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6633930Z [command]/usr/local/bin/git config --global --add safe.directory /Users/runner/work/tbc-tools/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6774220Z Deleting the contents of '/Users/runner/work/tbc-tools/tbc-tools'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6778760Z ##[group]Initializing the repository
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.6784360Z [command]/usr/local/bin/git init /Users/runner/work/tbc-tools/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7045300Z hint: Using 'master' as the name for the initial branch. This default branch name
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7047350Z hint: will change to "main" in Git 3.0. To configure the initial branch name
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7049430Z hint: to use in all of your new repositories, which will suppress this warning,
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7051030Z hint: call:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7052270Z hint:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7053180Z hint: 	git config --global init.defaultBranch <name>
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7054240Z hint:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7055430Z hint: Names commonly chosen instead of 'master' are 'main', 'trunk' and
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7056920Z hint: 'development'. The just-created branch can be renamed via this command:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7058170Z hint:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7059380Z hint: 	git branch -m <name>
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7060460Z hint:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7061910Z hint: Disable this message with "git config set advice.defaultBranchName false"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7063990Z Initialized empty Git repository in /Users/runner/work/tbc-tools/tbc-tools/.git/
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7067780Z [command]/usr/local/bin/git remote add origin https://github.com/harrypm/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7203110Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7204500Z ##[group]Disabling automatic garbage collection
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7205540Z [command]/usr/local/bin/git config --local gc.auto 0
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7275460Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7276640Z ##[group]Setting up auth
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7282910Z [command]/usr/local/bin/git config --local --name-only --get-regexp core\.sshCommand
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.7380430Z [command]/usr/local/bin/git submodule foreach --recursive sh -c "git config --local --name-only --get-regexp 'core\.sshCommand' && git config --local --unset-all 'core.sshCommand' || :"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.8691670Z [command]/usr/local/bin/git config --local --name-only --get-regexp http\.https\:\/\/github\.com\/\.extraheader
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.8797200Z [command]/usr/local/bin/git submodule foreach --recursive sh -c "git config --local --name-only --get-regexp 'http\.https\:\/\/github\.com\/\.extraheader' && git config --local --unset-all 'http.https://github.com/.extraheader' || :"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:20.9980310Z [command]/usr/local/bin/git config --local --name-only --get-regexp ^includeIf\.gitdir:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:21.0073910Z [command]/usr/local/bin/git submodule foreach --recursive git config --local --show-origin --name-only --get-regexp remote.origin.url
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:21.2028580Z [command]/usr/local/bin/git config --local http.https://github.com/.extraheader AUTHORIZATION: basic ***
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:21.2049430Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:21.2051590Z ##[group]Fetching the repository
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:21.2054350Z [command]/usr/local/bin/git -c protocol.version=2 fetch --prune --no-recurse-submodules origin +refs/heads/*:refs/remotes/origin/* +refs/tags/*:refs/tags/*
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.3728950Z From https://github.com/harrypm/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.3731500Z  * [new branch]      main        -> origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.3734660Z  * [new tag]         v7.2.0-test -> v7.2.0-test
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.3872310Z [command]/usr/local/bin/git branch --list --remote origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.3957350Z   origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.3965700Z [command]/usr/local/bin/git rev-parse refs/remotes/origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.4045600Z ea8df727b6b0efcb56f39a10975aa373cf4e0283
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.4051000Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.4053290Z ##[group]Determining the checkout info
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.4054980Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.4056420Z [command]/usr/local/bin/git sparse-checkout disable
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.4169910Z [command]/usr/local/bin/git config --local --unset-all extensions.worktreeConfig
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.4262740Z ##[group]Checking out the ref
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:24.4266830Z [command]/usr/local/bin/git checkout --progress --force -B main refs/remotes/origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.3842770Z Switched to a new branch 'main'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.3849240Z branch 'main' set up to track 'origin/main'.
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.3894270Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.3895090Z ##[group]Setting up auth for fetching submodules
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.3899210Z [command]/usr/local/bin/git config --global http.https://github.com/.extraheader AUTHORIZATION: basic ***
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.4006540Z [command]/usr/local/bin/git config --global --unset-all url.https://github.com/.insteadOf
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.4096680Z [command]/usr/local/bin/git config --global --add url.https://github.com/.insteadOf git@github.com:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.4191650Z [command]/usr/local/bin/git config --global --add url.https://github.com/.insteadOf org-56382624@github.com:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.4280210Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.4280910Z ##[group]Fetching submodules
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.4284740Z [command]/usr/local/bin/git submodule sync
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.5425340Z [command]/usr/local/bin/git -c protocol.version=2 submodule update --init --force
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.6649730Z Submodule 'src/efm-decoder/libs/ezpwd' (https://github.com/pjkundert/ezpwd-reed-solomon.git) registered for path 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:25.6825530Z Cloning into '/Users/runner/work/tbc-tools/tbc-tools/src/efm-decoder/libs/ezpwd'...
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.1428500Z Submodule path 'src/efm-decoder/libs/ezpwd': checked out '62a490c13f6e057fbf2dc6777fde234c7a19098e'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.1447180Z [command]/usr/local/bin/git submodule foreach git config --local gc.auto 0
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.2671920Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.2844500Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.2845790Z ##[group]Persisting credentials for submodules
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.2850860Z [command]/usr/local/bin/git submodule foreach --recursive sh -c "git config --local --name-only --get-regexp 'url\.https\:\/\/github\.com\/\.insteadOf' && git config --local --unset-all 'url.https://github.com/.insteadOf' || :"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.4044610Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.4390540Z [command]/usr/local/bin/git submodule foreach sh -c "git config --local 'http.https://github.com/.extraheader' 'AUTHORIZATION: basic ***' && git config --local --show-origin --name-only --get-regexp remote.origin.url"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.5755820Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.6122930Z file:/Users/runner/work/tbc-tools/tbc-tools/.git/modules/src/efm-decoder/libs/ezpwd/config	remote.origin.url
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.6158950Z [command]/usr/local/bin/git submodule foreach git config --local --add 'url.https://github.com/.insteadOf' 'git@github.com:'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.7310610Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.7472120Z [command]/usr/local/bin/git submodule foreach git config --local --add 'url.https://github.com/.insteadOf' 'org-56382624@github.com:'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.8608620Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.8785870Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.8914340Z [command]/usr/local/bin/git log -1 --format=%H
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:32:27.9034150Z ea8df727b6b0efcb56f39a10975aa373cf4e0283
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9345390Z ##[group]Run cachix/install-nix-action@v27
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9345920Z with:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9346310Z   nix_path: nixpkgs=channel:nixos-unstable
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9346790Z   enable_kvm: true
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9347140Z ##[endgroup]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9455980Z ##[group]Run ${GITHUB_ACTION_PATH}/install-nix.sh
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9456630Z ${GITHUB_ACTION_PATH}/install-nix.sh
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9520110Z shell: /bin/bash --noprofile --norc -e -o pipefail {0}
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9521110Z env:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9521380Z   INPUT_EXTRA_NIX_CONFIG: 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9521750Z   INPUT_GITHUB_ACCESS_TOKEN: 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9522120Z   INPUT_INSTALL_OPTIONS: 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9522470Z   INPUT_INSTALL_URL: 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9522880Z   INPUT_NIX_PATH: nixpkgs=channel:nixos-unstable
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9523370Z   INPUT_ENABLE_KVM: true
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9524130Z   GITHUB_TOKEN: ***
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9524420Z ##[endgroup]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:27.9824470Z ##[group]Installing Nix
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1530120Z installer options: --no-channel-add --darwin-use-unencrypted-nix-store-volume --nix-extra-conf-file /var/folders/jk/ysxqfpt14z38ldf2_9l9czvw0000gn/T/tmp.KH5ifunnbQ/nix.conf --daemon --daemon-user-count 8
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1870280Z * Host releases.nixos.org:443 was resolved.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1871200Z * IPv6: (none)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1871820Z * IPv4: 151.101.65.91, 151.101.1.91, 151.101.193.91, 151.101.129.91
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1872580Z *   Trying 151.101.65.91:443...
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1894350Z * ALPN: curl offers h2,http/1.1
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1896950Z } [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1897990Z * TLSv1.3 (OUT), TLS handshake, Client hello (1):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1898870Z } [1562 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1899520Z * SSL Trust Anchors:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.1900200Z *   Native: Apple SecTrust
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2023470Z *   OpenSSL default paths (fallback)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2024020Z { [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2024730Z * TLSv1.3 (IN), TLS handshake, Server hello (2):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2025430Z { [1210 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2044400Z * TLSv1.3 (IN), TLS change cipher, Change cipher spec (1):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2045170Z { [1 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2045710Z * TLSv1.3 (IN), TLS handshake, Encrypted Extensions (8):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2046580Z { [19 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2047440Z * TLSv1.3 (IN), TLS handshake, Certificate (11):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2047960Z { [2602 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2048490Z * TLSv1.3 (IN), TLS handshake, CERT verify (15):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2049030Z { [264 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2049400Z * TLSv1.3 (IN), TLS handshake, Finished (20):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2049990Z { [36 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2051310Z * TLSv1.3 (OUT), TLS change cipher, Change cipher spec (1):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2051930Z } [1 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2052390Z * TLSv1.3 (OUT), TLS handshake, Finished (20):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2052970Z } [36 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2053610Z * SSL connection using TLSv1.3 / TLS_AES_128_GCM_SHA256 / X25519MLKEM768 / RSASSA-PSS
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2054530Z * ALPN: server accepted h2
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2055160Z * Server certificate:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2055680Z *   subject: CN=releases.nixos.org
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2056330Z *   start date: Jan 13 01:58:01 2026 GMT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2056810Z *   expire date: Apr 13 01:58:00 2026 GMT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2057320Z *   issuer: C=US; O=Let's Encrypt; CN=R12
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2058250Z *   Certificate level 0: Public key type RSA (2048/112 Bits/secBits), signed using sha256WithRSAEncryption
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2059640Z *   Certificate level 1: Public key type RSA (2048/112 Bits/secBits), signed using sha256WithRSAEncryption
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2062270Z *   Certificate level 2: Public key type RSA (4096/152 Bits/secBits), signed using sha256WithRSAEncryption
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2063540Z *   subjectAltName: "releases.nixos.org" matches cert's "releases.nixos.org"
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2064560Z * SSL certificate verified via OpenSSL.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2066300Z * Established connection to releases.nixos.org (151.101.65.91 port 443) from 192.168.64.2 port 49169 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2067150Z * using HTTP/2
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2067720Z * [HTTP/2] [1] OPENED stream for https://releases.nixos.org/nix/nix-2.22.1/install
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2068480Z * [HTTP/2] [1] [:method: GET]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2068860Z * [HTTP/2] [1] [:scheme: https]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2069300Z * [HTTP/2] [1] [:authority: releases.nixos.org]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2070170Z * [HTTP/2] [1] [:path: /nix/nix-2.22.1/install]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2070640Z * [HTTP/2] [1] [user-agent: curl/8.18.0]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2071150Z * [HTTP/2] [1] [accept: */*]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2071530Z } [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2071910Z > GET /nix/nix-2.22.1/install HTTP/2
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2072360Z > Host: releases.nixos.org
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2072750Z > User-Agent: curl/8.18.0
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2073120Z > Accept: */*
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2073410Z > 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2073710Z * Request completely sent off
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2074100Z { [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2074490Z * TLSv1.3 (IN), TLS handshake, Newsession Ticket (4):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2075000Z { [193 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2080640Z < HTTP/2 200 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2081020Z < last-modified: Fri, 10 May 2024 09:28:12 GMT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2081530Z < etag: "d07247c3fa419cb02a37dcf14819c05a"
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2082260Z < x-amz-server-side-encryption: AES256
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2082890Z < content-type: text/plain
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2083410Z < server: AmazonS3
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2085060Z < via: 1.1 varnish, 1.1 varnish
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2086470Z < access-control-allow-origin: *
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2087130Z < accept-ranges: bytes
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2087850Z < age: 71067
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2088530Z < date: Thu, 26 Feb 2026 03:32:28 GMT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2089220Z < x-served-by: cache-dub4377-DUB, cache-sjc10044-SJC
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2089890Z < x-cache: HIT, HIT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2090580Z < x-cache-hits: 91, 0
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2091470Z < content-length: 4052
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2092100Z < 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2092660Z { [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2093520Z * Connection #0 to host releases.nixos.org:443 left intact
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2341780Z downloading Nix 2.22.1 binary tarball for x86_64-darwin from 'https://releases.nixos.org/nix/nix-2.22.1/nix-2.22.1-x86_64-darwin.tar.xz' to '/var/folders/jk/ysxqfpt14z38ldf2_9l9czvw0000gn/T/nix-binary-tarball-unpack.XXXXXXXXXX.ZxkfzCX21C'...
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2534450Z   % Total    % Received % Xferd  Average Speed  Time    Time    Time   Current
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2536270Z                                  Dload  Upload  Total   Spent   Left   Speed
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:28.2537880Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:29.2964980Z   0      0   0      0   0      0      0      0                              0
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:29.3185910Z  29 15.02M  29  4.41M   0      0  4.41M      0   00:03           00:03  4.41M
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:29.3264730Z 100 15.02M 100 15.02M   0      0 14.32M      0   00:01   00:01          4.41M
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:29.3357790Z 100 15.02M 100 15.02M   0      0 14.32M      0   00:01   00:01          4.41M
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:29.3453350Z 100 15.02M 100 15.02M   0      0 14.32M      0   00:01   00:01          4.41M
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.5316120Z Warning: the flag --darwin-use-unencrypted-nix-store-volume
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.5317550Z          is no longer needed and will be removed in the future.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.5318130Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.5357570Z Switching to the Multi-user Installer
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6084490Z Welcome to the Multi-User Nix Installation
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6120800Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6122010Z This installation tool will set up your computer with the Nix package
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6122820Z manager. This will happen in a few stages:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6123180Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6123490Z 1. Make sure your computer doesn't already have Nix. If it does, I
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6124490Z    will show you instructions on how to clean up your old install.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6125560Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6125860Z 2. Show you what I am going to install and where. Then I will ask
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6126530Z    if you are ready to continue.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6126830Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6127420Z 3. Create the system users (uids [301..308]) and groups (gid 30000)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6128330Z    that the Nix daemon uses to run builds. To create system users
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6129440Z    in a different range, exit and run this tool again with
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6130030Z    NIX_FIRST_BUILD_UID set.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6130320Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6130580Z 4. Perform the basic installation of the Nix files daemon.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6131030Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6131350Z 5. Configure your shell to import special Nix Profile files, so you
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6132030Z    can use Nix.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6132220Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6132380Z 6. Start the Nix daemon.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6132630Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6133050Z Would you like to see a more detailed list of what I will do?
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6133850Z No TTY, assuming you would say yes :)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6157990Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6159200Z I will:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6159450Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6159750Z  - make sure your computer doesn't already have Nix files
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6160460Z    (if it does, I will tell you how to clean them up.)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6161150Z  - create local users (see the list above for the users I'll make)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6161860Z  - create a local group (nixbld)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6162310Z  - install Nix in to /nix
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6162750Z  - create a configuration file in /etc/nix
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6163410Z  - set up the "default profile" by creating some Nix-related files in
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6164090Z    /var/root
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6196910Z  - back up /etc/bashrc to /etc/bashrc.backup-before-nix
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6197840Z  - update /etc/bashrc to include some Nix configuration
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6235390Z  - back up /etc/zshrc to /etc/zshrc.backup-before-nix
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6236380Z  - update /etc/zshrc to include some Nix configuration
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6237620Z  - create a Nix volume and a LaunchDaemon to mount it
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6238640Z  - create a LaunchDaemon (at /Library/LaunchDaemons/org.nixos.nix-daemon.plist) for nix-daemon
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6239390Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6239770Z Ready to continue?
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6240270Z No TTY, assuming you would say yes :)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6290620Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6291720Z ---- let's talk about sudo -----------------------------------------------------
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6326540Z This script is going to call sudo a lot. Normally, it would show you
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6327530Z exactly what commands it is running and why. However, the script is
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6328240Z run in a headless fashion, like this:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6328570Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6328750Z   $ curl -L https://nixos.org/nix/install | sh
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6329170Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6329490Z or maybe in a CI pipeline. Because of that, I'm going to skip the
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6330170Z verbose output in the interest of brevity.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6330630Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6330770Z If you would like to
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6331140Z see the output, try like this:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6331450Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6331700Z   $ curl -L -o install-nix https://nixos.org/nix/install
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6332300Z   $ sh ./install-nix
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6332520Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6332530Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6332780Z ~~> Fixing any leftover Nix volume state
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6363660Z Before I try to install, I'll check for any existing Nix volume config
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6364600Z and ask for your permission to remove it (so that the installer can
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6365470Z start fresh). I'll also ask for permission to fix any issues I spot.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6633600Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6634400Z ~~> Checking for artifacts of previous installs
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6670080Z Before I try to install, I'll check for signs Nix already is or has
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6670840Z been installed on this system.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6755160Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6756100Z ---- Nix config report ---------------------------------------------------------
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6757220Z         Temp Dir:	/var/folders/jk/ysxqfpt14z38ldf2_9l9czvw0000gn/T/tmp.VsivRyZFHF
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6758990Z         Nix Root:	/nix
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6759650Z      Build Users:	8
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6760100Z   Build Group ID:	30000
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6760600Z Build Group Name:	nixbld
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6760900Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6761070Z build users:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6761500Z     Username:	UID
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6811090Z      _nixbld1:	301
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6831510Z      _nixbld2:	302
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6851060Z      _nixbld3:	303
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6870160Z      _nixbld4:	304
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6889050Z      _nixbld5:	305
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6907850Z      _nixbld6:	306
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6927010Z      _nixbld7:	307
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6946190Z      _nixbld8:	308
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6946490Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6946700Z Ready to continue?
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6947220Z No TTY, assuming you would say yes :)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6988700Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.6989750Z ---- Preparing a Nix volume ----------------------------------------------------
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.7024530Z     Nix traditionally stores its data in the root directory /nix, but
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.7025520Z     macOS now (starting in 10.15 Catalina) has a read-only root directory.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.7026600Z     To support Nix, I will create a volume and configure macOS to mount it
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.7027280Z     at /nix.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.7061970Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.7063530Z ~~> Configuring /etc/synthetic.conf to make a mount-point at /nix
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.8089040Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:31.8089500Z ~~> Creating a Nix volume
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:32.6666760Z disk1s7 was already unmounted
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:32.6788820Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:32.6790030Z ~~> Configuring /etc/fstab to specify volume mount options
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:32.7804780Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:32.7807890Z ~~> Configuring LaunchDaemon to mount 'Nix Store'
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.2878270Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.2879090Z ~~> Setting up the build group nixbld
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.3405500Z             Created:	Yes
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.3461310Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.3462400Z ~~> Setting up the build user _nixbld1
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4683500Z <main> attribute status: eDSRecordAlreadyExists
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4684220Z <dscl_cmd> DS Error: -14135 (eDSRecordAlreadyExists)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4789750Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4790540Z ---- oh no! --------------------------------------------------------------------
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4822060Z Oh no, something went wrong. If you can take all the output and open
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4823180Z an issue, we'd love to fix the problem so nobody else has this issue.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4823700Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4824120Z :(
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4824800Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4836610Z We'd love to help if you need it.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4843040Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4844600Z You can open an issue at
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4845870Z https://github.com/NixOS/nix/issues/new?labels=installer&template=installer.md
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4847150Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.4848050Z Or get in touch with the community: https://nixos.org/community
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.7550020Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:32:43.7570070Z ##[error]Process completed with exit code 1.
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:43.7866410Z Post job cleanup.
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:43.9190340Z [command]/usr/local/bin/git version
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:43.9286240Z git version 2.53.0
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:43.9324810Z Copying '/Users/runner/.gitconfig' to '/Users/runner/work/_temp/d63c8014-b97e-4f24-bb4d-e3597a622ed3/.gitconfig'
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:43.9340110Z Temporarily overriding HOME='/Users/runner/work/_temp/d63c8014-b97e-4f24-bb4d-e3597a622ed3' before making global git config changes
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:43.9341660Z Adding repository directory to the temporary git global config as a safe directory
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:43.9346890Z [command]/usr/local/bin/git config --global --add safe.directory /Users/runner/work/tbc-tools/tbc-tools
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:43.9482820Z [command]/usr/local/bin/git config --local --name-only --get-regexp core\.sshCommand
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:43.9554630Z [command]/usr/local/bin/git submodule foreach --recursive sh -c "git config --local --name-only --get-regexp 'core\.sshCommand' && git config --local --unset-all 'core.sshCommand' || :"
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.0696010Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.1039010Z [command]/usr/local/bin/git config --local --name-only --get-regexp http\.https\:\/\/github\.com\/\.extraheader
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.1123050Z http.https://github.com/.extraheader
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.1292240Z [command]/usr/local/bin/git config --local --unset-all http.https://github.com/.extraheader
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.1296590Z [command]/usr/local/bin/git submodule foreach --recursive sh -c "git config --local --name-only --get-regexp 'http\.https\:\/\/github\.com\/\.extraheader' && git config --local --unset-all 'http.https://github.com/.extraheader' || :"
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.2457200Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.2684890Z http.https://github.com/.extraheader
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.2900590Z [command]/usr/local/bin/git config --local --name-only --get-regexp ^includeIf\.gitdir:
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.3663460Z [command]/usr/local/bin/git submodule foreach --recursive git config --local --show-origin --name-only --get-regexp remote.origin.url
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.4642600Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.4801270Z file:/Users/runner/work/tbc-tools/tbc-tools/.git/modules/src/efm-decoder/libs/ezpwd/config	remote.origin.url
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:32:44.4920440Z [command]/usr/local/bin/git config --file /Users/runner/work/tbc-tools/tbc-tools/.git/modules/src/efm-decoder/libs/ezpwd/config --name-only --get-regexp ^includeIf\.gitdir:
Build tbc-tools (x86_64)	Complete job	2026-02-26T03:32:44.5205860Z Cleaning up orphan processes
```

## Files Modified
- `.github/workflows/build_macos_tools.yml` (added `install_options: --no-daemon` to Nix install step)

## Additional Commands and Output (Iteration 2)

### Command
```bash
git add .github/workflows/build_macos_tools.yml prompt_readme.md
```

### Output
```
```

### Command
```bash
git commit -m "Use single-user Nix install on macOS runners

- Pass --no-daemon to nix installer to avoid nixbld user conflicts
- Add prompt log for CI investigation

Co-Authored-By: Oz <oz-agent@warp.dev>"
```

### Output
```
[main d8b9ab6b] Use single-user Nix install on macOS runners
 2 files changed, 561 insertions(+)
 create mode 100644 prompt_readme.md
```

### Command
```bash
git push origin main
```

### Output
```
Enumerating objects: 10, done.
Counting objects: 100% (10/10), done.
Delta compression using up to 8 threads
Compressing objects: 100% (5/5), done.
Writing objects: 100% (6/6), 10.64 KiB | 10.64 MiB/s, done.
Total 6 (delta 3), reused 0 (delta 0), pack-reused 0 (from 0)
remote: Resolving deltas: 100% (3/3), completed with 3 local objects.
To https://github.com/harrypm/tbc-tools.git
   ea8df727..d8b9ab6b  main -> main
```

### Command
```bash
gh workflow run "Build macOS tools" --ref main
```

### Output
```
✓ Created workflow_dispatch event for build_macos_tools.yml at main

To see runs for this workflow, try: gh run list --workflow="build_macos_tools.yml"
```

### Command
```bash
gh run list --workflow "build_macos_tools.yml" --limit 1 --json databaseId,status,conclusion,headSha,createdAt
```

### Output
```json
[
  {
    "conclusion": "",
    "createdAt": "2026-02-26T03:40:58Z",
    "databaseId": 22426819839,
    "headSha": "d8b9ab6b752920759254847fc25691901e4e3f09",
    "status": "in_progress"
  }
]
```

### Command
```bash
gh run watch 22426819839
```

### Output
```
X main Build macOS tools · 22426819839
Triggered via workflow_dispatch less than a minute ago

JOBS
X Build tbc-tools (arm64) in 15s (ID 64936722139)
  ✓ Set up job
  ✓ Run actions/checkout@v4
  X Install Nix
  - Build with Nix
  - Get version info
  - Create app bundle structure
  - Create app icon
  - Bundle dependencies
  - Install create-dmg
  - Verify app bundle
  - Create DMG
  - Upload binary artifact
  ✓ Post Run actions/checkout@v4
  ✓ Complete job
X Build tbc-tools (x86_64) in 13s (ID 64936722147)
  ✓ Set up job
  ✓ Run actions/checkout@v4
  X Install Nix
  - Build with Nix
  - Get version info
  - Create app bundle structure
  - Create app icon
  - Bundle dependencies
  - Install create-dmg
  - Verify app bundle
  - Create DMG
  - Upload binary artifact
  ✓ Post Run actions/checkout@v4
  ✓ Complete job

ANNOTATIONS
X Process completed with exit code 1.
Build tbc-tools (x86_64): .github#107


X Run Build macOS tools (22426819839) completed with 'failure'
%                                      
```

### Command
```bash
gh run view 22426819839 --log --job 64936722147
```

### Output
```
! both run and job IDs specified; ignoring run ID
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9532150Z Current runner version: '2.331.0'
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9560640Z ##[group]Runner Image Provisioner
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9561510Z Hosted Compute Agent
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9562100Z Version: 20260213.493
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9562740Z Commit: 5c115507f6dd24b8de37d8bbe0bb4509d0cc0fa3
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9563520Z Build Date: 2026-02-13T00:28:41Z
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9564240Z Worker ID: {ab0889a8-c7cb-470e-a67a-92b9785d1725}
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9565010Z Azure Region: westus
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9565570Z ##[endgroup]
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9567400Z ##[group]Operating System
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9568030Z macOS
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9568500Z 15.7.4
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9568960Z 24G517
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9569440Z ##[endgroup]
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9569950Z ##[group]Runner Image
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9570510Z Image: macos-15
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9571020Z Version: 20260217.0171.1
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9572290Z Included Software: https://github.com/actions/runner-images/blob/macos-15/20260217.0171/images/macos/macos-15-Readme.md
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9574300Z Image Release: https://github.com/actions/runner-images/releases/tag/macos-15%2F20260217.0171
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9575460Z ##[endgroup]
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9579220Z ##[group]GITHUB_TOKEN Permissions
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9581580Z Actions: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9582170Z ArtifactMetadata: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9582770Z Attestations: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9583430Z Checks: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9583970Z Contents: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9584500Z Deployments: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9585060Z Discussions: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9585600Z Issues: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9586120Z Metadata: read
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9586640Z Models: read
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9587160Z Packages: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9587690Z Pages: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9588320Z PullRequests: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9588920Z RepositoryProjects: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9589540Z SecurityEvents: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9590120Z Statuses: write
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9590640Z ##[endgroup]
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9593240Z Secret source: Actions
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9594100Z Prepare workflow directory
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:02.9975660Z Prepare all required actions
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:03.0019050Z Getting action download info
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:03.2721260Z Download action repository 'actions/checkout@v4' (SHA:34e114876b0b11c390a56381ad16ebd13914f8d5)
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:03.5210600Z Download action repository 'cachix/install-nix-action@v27' (SHA:ba0dd844c9180cbf77aa72a116d6fbc515d0e87b)
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:03.9134670Z Download action repository 'actions/upload-artifact@v4' (SHA:ea165f8d65b6e75b540449e92b4886f43607fa02)
Build tbc-tools (x86_64)	Set up job	2026-02-26T03:41:04.2121210Z Complete job name: Build tbc-tools (x86_64)
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3008450Z ##[group]Run actions/checkout@v4
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3010030Z with:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3010560Z   fetch-depth: 0
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3011120Z   fetch-tags: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3011680Z   submodules: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3012260Z   repository: harrypm/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3013190Z   token: ***
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3013830Z   ssh-strict: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3014390Z   ssh-user: git
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3015080Z   persist-credentials: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3015750Z   clean: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3016340Z   sparse-checkout-cone-mode: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3017020Z   show-progress: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3017600Z   lfs: false
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3018140Z   set-safe-directory: true
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.3019360Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.5651940Z Syncing repository: harrypm/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.5655530Z ##[group]Getting Git version info
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.5657030Z Working directory is '/Users/runner/work/tbc-tools/tbc-tools'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.5660430Z [command]/usr/local/bin/git version
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6006880Z git version 2.53.0
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6038740Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6052190Z Copying '/Users/runner/.gitconfig' to '/Users/runner/work/_temp/fdc6408a-c9d4-4141-8dc9-15f1b3941f07/.gitconfig'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6070120Z Temporarily overriding HOME='/Users/runner/work/_temp/fdc6408a-c9d4-4141-8dc9-15f1b3941f07' before making global git config changes
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6075030Z Adding repository directory to the temporary git global config as a safe directory
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6087180Z [command]/usr/local/bin/git config --global --add safe.directory /Users/runner/work/tbc-tools/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6213260Z Deleting the contents of '/Users/runner/work/tbc-tools/tbc-tools'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6219300Z ##[group]Initializing the repository
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6226110Z [command]/usr/local/bin/git init /Users/runner/work/tbc-tools/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6519880Z hint: Using 'master' as the name for the initial branch. This default branch name
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6524040Z hint: will change to \"main\" in Git 3.0. To configure the initial branch name
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6528590Z hint: to use in all of your new repositories, which will suppress this warning,
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6532170Z hint: call:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6533820Z hint:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6535840Z hint: 	git config --global init.defaultBranch <name>
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6538540Z hint:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6541010Z hint: Names commonly chosen instead of 'master' are 'main', 'trunk' and
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6545380Z hint: 'development'. The just-created branch can be renamed via this command:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6549080Z hint:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6551030Z hint: 	git branch -m <name>
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6553140Z hint:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6556350Z hint: Disable this message with \"git config set advice.defaultBranchName false\"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6561150Z Initialized empty Git repository in /Users/runner/work/tbc-tools/tbc-tools/.git/
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6569260Z [command]/usr/local/bin/git remote add origin https://github.com/harrypm/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6687540Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6690690Z ##[group]Disabling automatic garbage collection
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6693240Z [command]/usr/local/bin/git config --local gc.auto 0
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6802180Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6804410Z ##[group]Setting up auth
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6810000Z [command]/usr/local/bin/git config --local --name-only --get-regexp core\\.sshCommand
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.6913900Z [command]/usr/local/bin/git submodule foreach --recursive sh -c \"git config --local --name-only --get-regexp 'core\\.sshCommand' && git config --local --unset-all 'core.sshCommand' || :\"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.8330350Z [command]/usr/local/bin/git config --local --name-only --get-regexp http\\.https\\:\\/\\/github\\.com\\/\\.extraheader
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.8432500Z [command]/usr/local/bin/git submodule foreach --recursive sh -c \"git config --local --name-only --get-regexp 'http\\.https\\:\\/\\/github\\.com\\/\\.extraheader' && git config --local --unset-all 'http.https://github.com/.extraheader' || :\"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.9601780Z [command]/usr/local/bin/git config --local --name-only --get-regexp ^includeIf\\.gitdir:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:04.9699970Z [command]/usr/local/bin/git submodule foreach --recursive git config --local --show-origin --name-only --get-regexp remote.origin.url
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:05.1400330Z [command]/usr/local/bin/git config --local http.https://github.com/.extraheader AUTHORIZATION: basic ***
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:05.1495600Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:05.1526550Z ##[group]Fetching the repository
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:05.1560550Z [command]/usr/local/bin/git -c protocol.version=2 fetch --prune --no-recurse-submodules origin +refs/heads/*:refs/remotes/origin/* +refs/tags/*:refs/tags/*
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1207600Z From https://github.com/harrypm/tbc-tools
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1211560Z  * [new branch]      main        -> origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1212970Z  * [new tag]         v7.2.0-test -> v7.2.0-test
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1354700Z [command]/usr/local/bin/git branch --list --remote origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1438350Z   origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1448620Z [command]/usr/local/bin/git rev-parse refs/remotes/origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1530410Z d8b9ab6b752920759254847fc25691901e4e3f09
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1535740Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1537370Z ##[group]Determining the checkout info
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1538280Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1542720Z [command]/usr/local/bin/git sparse-checkout disable
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1651030Z [command]/usr/local/bin/git config --local --unset-all extensions.worktreeConfig
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1742260Z ##[group]Checking out the ref
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:08.1747810Z [command]/usr/local/bin/git checkout --progress --force -B main refs/remotes/origin/main
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.1856450Z Switched to a new branch 'main'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.1863990Z branch 'main' set up to track 'origin/main'.
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.1911730Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.1913070Z ##[group]Setting up auth for fetching submodules
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.1917780Z [command]/usr/local/bin/git config --global http.https://github.com/.extraheader AUTHORIZATION: basic ***
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.2048650Z [command]/usr/local/bin/git config --global --unset-all url.https://github.com/.insteadOf
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.2144800Z [command]/usr/local/bin/git config --global --add url.https://github.com/.insteadOf git@github.com:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.2265440Z [command]/usr/local/bin/git config --global --add url.https://github.com/.insteadOf org-56382624@github.com:
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.2376880Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.2377810Z ##[group]Fetching submodules
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.2382500Z [command]/usr/local/bin/git submodule sync
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.3608340Z [command]/usr/local/bin/git -c protocol.version=2 submodule update --init --force
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.5026030Z Submodule 'src/efm-decoder/libs/ezpwd' (https://github.com/pjkundert/ezpwd-reed-solomon.git) registered for path 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:09.5218790Z Cloning into '/Users/runner/work/tbc-tools/tbc-tools/src/efm-decoder/libs/ezpwd'...
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.1926490Z Submodule path 'src/efm-decoder/libs/ezpwd': checked out '62a490c13f6e057fbf2dc6777fde234c7a19098e'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.1945100Z [command]/usr/local/bin/git submodule foreach git config --local gc.auto 0
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.3182880Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.3358050Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.3359920Z ##[group]Persisting credentials for submodules
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.3363950Z [command]/usr/local/bin/git submodule foreach --recursive sh -c \"git config --local --name-only --get-regexp 'url\\.https\\:\\/\\/github\\.com\\/\\.insteadOf' && git config --local --unset-all 'url.https://github.com/.insteadOf' || :\"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.4604630Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.5595070Z [command]/usr/local/bin/git submodule foreach sh -c \"git config --local 'http.https://github.com/.extraheader' 'AUTHORIZATION: basic ***' && git config --local --show-origin --name-only --get-regexp remote.origin.url\"
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.6587250Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.6898220Z file:/Users/runner/work/tbc-tools/tbc-tools/.git/modules/src/efm-decoder/libs/ezpwd/config	remote.origin.url
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.6958610Z [command]/usr/local/bin/git submodule foreach git config --local --add 'url.https://github.com/.insteadOf' 'git@github.com:'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.8181710Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.8352800Z [command]/usr/local/bin/git submodule foreach git config --local --add 'url.https://github.com/.insteadOf' 'org-56382624@github.com:'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.9576470Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.9740380Z ##[endgroup]
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.9866870Z [command]/usr/local/bin/git log -1 --format=%H
Build tbc-tools (x86_64)	Run actions/checkout@v4	2026-02-26T03:41:10.9971360Z d8b9ab6b752920759254847fc25691901e4e3f09
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0282190Z ##[group]Run cachix/install-nix-action@v27
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0282890Z with:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0283310Z   nix_path: nixpkgs=channel:nixos-unstable
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0283860Z   install_options: --no-daemon
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0284280Z   enable_kvm: true
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0284590Z ##[endgroup]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0406910Z ##[group]Run ${GITHUB_ACTION_PATH}/install-nix.sh
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0407640Z ${GITHUB_ACTION_PATH}/install-nix.sh
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0477810Z shell: /bin/bash --noprofile --norc -e -o pipefail {0}
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0478380Z env:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0478660Z   INPUT_EXTRA_NIX_CONFIG: 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0479040Z   INPUT_GITHUB_ACCESS_TOKEN: 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0479430Z   INPUT_INSTALL_OPTIONS: --no-daemon
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0479860Z   INPUT_INSTALL_URL: 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0480260Z   INPUT_NIX_PATH: nixpkgs=channel:nixos-unstable
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0480750Z   INPUT_ENABLE_KVM: true
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0481570Z   GITHUB_TOKEN: ***
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0481870Z ##[endgroup]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.0804060Z ##[group]Installing Nix
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.1475230Z installer options: --no-daemon --no-channel-add --darwin-use-unencrypted-nix-store-volume --nix-extra-conf-file /var/folders/jk/ysxqfpt14z38ldf2_9l9czvw0000gn/T/tmp.OlSgoV17mh/nix.conf
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.1782000Z * Host releases.nixos.org:443 was resolved.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2116120Z * IPv6: (none)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2148970Z * IPv4: 151.101.1.91, 151.101.193.91, 151.101.65.91, 151.101.129.91
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2192430Z *   Trying 151.101.1.91:443...
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2207770Z * ALPN: curl offers h2,http/1.1
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2219530Z } [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2263340Z * TLSv1.3 (OUT), TLS handshake, Client hello (1):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2285410Z } [1562 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2345960Z * SSL Trust Anchors:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2362740Z *   Native: Apple SecTrust
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2416390Z *   OpenSSL default paths (fallback)
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2425930Z { [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2488200Z * TLSv1.3 (IN), TLS handshake, Server hello (2):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2490350Z downloading Nix 2.22.1 binary tarball for x86_64-darwin from 'https://releases.nixos.org/nix/nix-2.22.1/nix-2.22.1-x86_64-darwin.tar.xz' to '/var/folders/jk/ysxqfpt14z38ldf2_9l9czvw0000gn/T/nix-binary-tarball-unpack.XXXXXXXXXX.C5dbpyWcop'...
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2492580Z { [1210 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2493100Z * TLSv1.3 (IN), TLS change cipher, Change cipher spec (1):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2493780Z { [1 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2494310Z * TLSv1.3 (IN), TLS handshake, Encrypted Extensions (8):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2494930Z { [19 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2495350Z * TLSv1.3 (IN), TLS handshake, Certificate (11):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2495970Z { [2602 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2496380Z * TLSv1.3 (IN), TLS handshake, CERT verify (15):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2496950Z { [264 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2497370Z * TLSv1.3 (IN), TLS handshake, Finished (20):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2497920Z { [36 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2498410Z * TLSv1.3 (OUT), TLS change cipher, Change cipher spec (1):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2517770Z } [1 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2518260Z * TLSv1.3 (OUT), TLS handshake, Finished (20):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2518770Z } [36 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2519650Z * SSL connection using TLSv1.3 / TLS_AES_128_GCM_SHA256 / X25519MLKEM768 / RSASSA-PSS
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2520470Z * ALPN: server accepted h2
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2520990Z * Server certificate:
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2521370Z *   subject: CN=releases.nixos.org
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2521850Z *   start date: Jan 13 01:58:01 2026 GMT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2522320Z *   expire date: Apr 13 01:58:00 2026 GMT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2522840Z *   issuer: C=US; O=Let's Encrypt; CN=R12
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2523730Z *   Certificate level 0: Public key type RSA (2048/112 Bits/secBits), signed using sha256WithRSAEncryption
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2525000Z *   Certificate level 1: Public key type RSA (2048/112 Bits/secBits), signed using sha256WithRSAEncryption
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2527180Z *   Certificate level 2: Public key type RSA (4096/152 Bits/secBits), signed using sha256WithRSAEncryption
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2528350Z *   subjectAltName: \"releases.nixos.org\" matches cert's \"releases.nixos.org\"
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2529080Z * SSL certificate verified via OpenSSL.
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2529910Z * Established connection to releases.nixos.org (151.101.1.91 port 443) from 192.168.64.3 port 49169 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2531150Z * using HTTP/2
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2531740Z * [HTTP/2] [1] OPENED stream for https://releases.nixos.org/nix/nix-2.22.1/install
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2532470Z * [HTTP/2] [1] [:method: GET]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2532940Z * [HTTP/2] [1] [:scheme: https]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2533510Z * [HTTP/2] [1] [:authority: releases.nixos.org]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2534200Z * [HTTP/2] [1] [:path: /nix/nix-2.22.1/install]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2534710Z * [HTTP/2] [1] [user-agent: curl/8.18.0]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2535190Z * [HTTP/2] [1] [accept: */*]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2535610Z } [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2535960Z > GET /nix/nix-2.22.1/install HTTP/2
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2536930Z > Host: releases.nixos.org
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2537660Z > User-Agent: curl/8.18.0
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2538090Z > Accept: */*
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2538770Z > 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2539100Z * Request completely sent off
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2539520Z { [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2539920Z * TLSv1.3 (IN), TLS handshake, Newsession Ticket (4):
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2540480Z { [193 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2540810Z < HTTP/2 200 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2541180Z < last-modified: Fri, 10 May 2024 09:28:12 GMT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2541670Z < etag: \"d07247c3fa419cb02a37dcf14819c05a\"
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2542150Z < x-amz-server-side-encryption: AES256
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2542620Z < content-type: text/plain
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2543040Z < server: AmazonS3
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2543380Z < via: 1.1 varnish, 1.1 varnish
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2543860Z < access-control-allow-origin: *
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2544320Z < accept-ranges: bytes
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2544730Z < date: Thu, 26 Feb 2026 03:41:12 GMT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2545200Z < age: 71591
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2545660Z < x-served-by: cache-dub4340-DUB, cache-iad-kiad7000130-IAD
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2546280Z < x-cache: HIT, HIT
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2546620Z < x-cache-hits: 1, 1
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2547020Z < content-length: 4052
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2547370Z < 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2547670Z { [5 bytes data]
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2548140Z * Connection #0 to host releases.nixos.org:443 left intact
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2704490Z   % Total    % Received % Xferd  Average Speed  Time    Time    Time   Current
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2706080Z                                  Dload  Upload  Total   Spent   Left   Speed
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.2706630Z 
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.3821670Z   0      0   0      0   0      0      0      0                              0
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.3822470Z 100 15.02M 100 15.02M   0      0 134.3M      0                              0
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.3823240Z 100 15.02M 100 15.02M   0      0 134.2M      0                              0
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:11.3824210Z 100 15.02M 100 15.02M   0      0 134.1M      0                              0
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:13.6064600Z Error: --no-daemon installs are no-longer supported on Darwin/macOS!
Build tbc-tools (x86_64)	Install Nix	2026-02-26T03:41:13.8728960Z ##[error]Process completed with exit code 1.
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:13.8985950Z Post job cleanup.
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.0298030Z [command]/usr/local/bin/git version
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.0401340Z git version 2.53.0
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.0447080Z Copying '/Users/runner/.gitconfig' to '/Users/runner/work/_temp/bb212e86-f3cb-4edd-9e6c-0ada94b815a2/.gitconfig'
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.0467820Z Temporarily overriding HOME='/Users/runner/work/_temp/bb212e86-f3cb-4edd-9e6c-0ada94b815a2' before making global git config changes
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.0469940Z Adding repository directory to the temporary git global config as a safe directory
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.0484850Z [command]/usr/local/bin/git config --global --add safe.directory /Users/runner/work/tbc-tools/tbc-tools
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.0601410Z [command]/usr/local/bin/git config --local --name-only --get-regexp core\\.sshCommand
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.0708020Z [command]/usr/local/bin/git submodule foreach --recursive sh -c \"git config --local --name-only --get-regexp 'core\\.sshCommand' && git config --local --unset-all 'core.sshCommand' || :\"
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.1943120Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.2329640Z [command]/usr/local/bin/git config --local --name-only --get-regexp http\\.https\\:\\/\\/github\\.com\\/\\.extraheader
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.2419170Z http.https://github.com/.extraheader
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.2434970Z [command]/usr/local/bin/git config --local --unset-all http.https://github.com/.extraheader
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.2596210Z [command]/usr/local/bin/git submodule foreach --recursive sh -c \"git config --local --name-only --get-regexp 'http\\.https\\:\\/\\/github\\.com\\/\\.extraheader' && git config --local --unset-all 'http.https://github.com/.extraheader' || :\"
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.3812550Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.4120460Z http.https://github.com/.extraheader
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.4489580Z [command]/usr/local/bin/git config --local --name-only --get-regexp ^includeIf\\.gitdir:
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.4642890Z [command]/usr/local/bin/git submodule foreach --recursive git config --local --show-origin --name-only --get-regexp remote.origin.url
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.6181600Z Entering 'src/efm-decoder/libs/ezpwd'
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.6396450Z file:/Users/runner/work/tbc-tools/tbc-tools/.git/modules/src/efm-decoder/libs/ezpwd/config	remote.origin.url
Build tbc-tools (x86_64)	Post Run actions/checkout@v4	2026-02-26T03:41:14.7272160Z [command]/usr/local/bin/git config --file /Users/runner/work/tbc-tools/tbc-tools/.git/modules/src/efm-decoder/libs/ezpwd/config --name-only --get-regexp ^includeIf\\.gitdir:
Build tbc-tools (x86_64)	Complete job	2026-02-26T03:41:14.7357320Z Cleaning up orphan processes
```

### Command
```bash
curl -L https://nixos.org/nix/install -o /tmp/nix-install.sh
```

### Output
```
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
  0     0    0     0    0     0      0   0     0    0     0    0     0      0      0 --:--:-- --:--:-- --:--:--     0
100  4267  100  4267    0     0   8491      0 --:--:-- --:--:-- --:--:--  8491
```

### Command
```bash
grep -n "NIX_" /tmp/nix-install.sh | head -100
```

### Output
```
13:readonly NIX_BECOME="${NIX_BECOME:-sudo}"
73:            export NIX_INSTALLER_YES=1;;
75:            export NIX_INSTALLER_NO_CHANNEL_ADD=1;;
77:            export NIX_USER_COUNT="$2"
80:            NIX_INSTALLER_NO_MODIFY_PROFILE=1;;
89:            export NIX_EXTRA_CONF="$(cat "$2")"
148:    echo "directory $dest does not exist; creating it by running '$cmd' using $NIX_BECOME" >&2
149:    if ! $NIX_BECOME sh -c "$cmd"; then
200:NIX_LINK="$HOME/.nix-profile"
208:if [ -z "$NIX_SSL_CERT_FILE" ] || ! [ -f "$NIX_SSL_CERT_FILE" ]; then
210:    export NIX_SSL_CERT_FILE="$NIX_LINK/etc/ssl/certs/ca-bundle.crt"
214:if [ -z "$NIX_INSTALLER_NO_CHANNEL_ADD" ]; then
218:    if [ -z "$_NIX_INSTALLER_TEST" ]; then
228:p_sh=$NIX_LINK/etc/profile.d/nix.sh
229:p_fish=$NIX_LINK/etc/profile.d/nix.fish
230:if [ -z "$NIX_INSTALLER_NO_MODIFY_PROFILE" ]; then
```

### Command
```bash
head -n 120 /tmp/nix-install.sh
```

### Output (summarized by tool)
```
I ran head -n 120 /tmp/nix-install.sh and got the following key outcomes and details:

- Script description: It installs the Nix package manager by downloading a binary tarball and running its installer.
- Temporary workspace: tmpDir is a newly created temporary directory; cleanup on exit will remove it.
- Platform mapping (sample entries):
  - Linux x86_64: system=x86_64-linux, path=crkzp9q2agkinm7ma959j7bk98ipf88p/nix-2.33.3-x86_64-linux.tar.xz, hash=96c5035a63699a12301566fca7838bb1264c3b0894dc0f610101726753e52ac2
  - Darwin x86_64: system=x86_64-darwin, path=365v0j8idldbz1dxcx4gwcrx4bci8cg7/nix-2.33.3-x86_64-darwin.tar.xz, hash=fbadda2f407d73409b444ab21386b5f713947e474ee4f4f96a9cbc6ccd9f853b
  - Darwin arm64: system=aarch64-darwin, path=8cjgja44sy3plhsa2n43gqsm7zgbh7my/nix-2.33.3-aarch64-darwin.tar.xz, hash=0781837b43d0234933141f6305c8a8fec6d49f3d2b245ee5b7c4e5d2a4a39c38
  - (other Linux/Arch variants present with their respective hash and path)
- URL resolution: If no --tarball-url-prefix is provided, URL is https://releases.nixos.org/nix/nix-2.33.3/nix-2.33.3-$system.tar.xz
- Prerequisites checks: require_util ensures tar is available; on non-Darwin, xz is also required.
- Download mechanism: prefers curl, otherwise wget; will fail with a quoted error if neither is present.
- SHA-256 verification: computes hash2 via sha256sum, shasum, or openssl; fails with an exact hash mismatch message if it doesn’t match the expected hash.
- Unpacking: unpacks tarball into a temporary unpack directory, and expects an installation script at unpacked-dir/*/install; errors if missing.

Critical details captured (verbatim as present where applicable):
- Expected hashes (example): 
  - Linux.x86_64: 96c5035a63699a12301566fca7838bb1264c3b0894dc0f610101726753e52ac2
  - Darwin.x86_64: fbadda2f407d73409b444ab21386b5f713947e474ee4f4f96a9cbc6ccd9f853b
  - Darwin.arm64: 0781837b43d0234933141f6305c8a8fec6d49f3d2b245ee5b7c4e5d2a4a39c38
- Exact error/kose messages that appear in failures (examples to look up if needed):
  - "you do not have '$1' installed, which I need to $2"
  - "missing argument for --tarball-url-prefix"
  - "failed to download '$url'"
  - "SHA-256 hash mismatch in '$url'; expected $hash, got $hash2"
  - "installation script is missing from the binary tarball!"
- Download target: tarball path is $tmpDir/nix-2.33.3-$system.tar.xz

If you want, I can extract any specific data points (e.g., exact hash for your current system, the computed URL, or the path to the final installation script) once you indicate which system variant you care about.
```

### Command
```bash
TMPDIR=$(mktemp -d) && curl -L https://releases.nixos.org/nix/nix-2.33.3/nix-2.33.3-x86_64-darwin.tar.xz -o "$TMPDIR/nix.tar.xz" && tar -xf "$TMPDIR/nix.tar.xz" -C "$TMPDIR" && find "$TMPDIR" -maxdepth 3 -name install -type f | head -1
```

### Output
```
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
  0     0    0     0    0     0      0   5 28.0M    5 1518k    0     0  4110k  41 28.0M   41 11.5M    0     0  8581k  68 28.0M   68 19.1M    0     0  8283k  90 28.0M   90 25.2M    0     0  7677k 100 28.0M  100 28.0M    0     0  7588k      0  0:00:03  0:00:03 --:--:-- 7587k
/var/folders/24/p8ptqhqd1yz86f46_5nlwc400000gn/T/tmp.5MbMiXMXlE/nix-2.33.3-x86_64-darwin/install
```

### Command
```bash
INSTALL_SCRIPT=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install" -type f 2>/dev/null | head -1) && grep -n "NIX_" "$INSTALL_SCRIPT" | head -100
```

### Output
```
13:readonly NIX_BECOME="${NIX_BECOME:-sudo}"
73:            export NIX_INSTALLER_YES=1;;
75:            export NIX_INSTALLER_NO_CHANNEL_ADD=1;;
77:            export NIX_USER_COUNT="$2"
80:            NIX_INSTALLER_NO_MODIFY_PROFILE=1;;
89:            export NIX_EXTRA_CONF="$(cat "$2")"
148:    echo "directory $dest does not exist; creating it by running '$cmd' using $NIX_BECOME" >&2
149:    if ! $NIX_BECOME sh -c "$cmd"; then
200:NIX_LINK="$HOME/.nix-profile"
208:if [ -z "$NIX_SSL_CERT_FILE" ] || ! [ -f "$NIX_SSL_CERT_FILE" ]; then
210:    export NIX_SSL_CERT_FILE="$NIX_LINK/etc/ssl/certs/ca-bundle.crt"
214:if [ -z "$NIX_INSTALLER_NO_CHANNEL_ADD" ]; then
218:    if [ -z "$_NIX_INSTALLER_TEST" ]; then
228:p_sh=$NIX_LINK/etc/profile.d/nix.sh
229:p_fish=$NIX_LINK/etc/profile.d/nix.fish
230:if [ -z "$NIX_INSTALLER_NO_MODIFY_PROFILE" ]; then
```

### Command
```bash
INSTALL_SCRIPT=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install" -type f 2>/dev/null | head -1) && sed -n '90,160p' "$INSTALL_SCRIPT"
```

### Output
```
            shift;;
        *)
            {
                echo "Nix Installer [--daemon|--no-daemon] [--daemon-user-count INT] [--yes] [--no-channel-add] [--no-modify-profile] [--nix-extra-conf-file FILE]"

                echo "Choose installation method."
                echo ""
                echo " --daemon:    Installs and configures a background daemon that manages the store,"
                echo "              providing multi-user support and better isolation for local builds."
                echo "              Both for security and reproducibility, this method is recommended if"
                echo "              supported on your platform."
                echo "              See https://nix.dev/manual/nix/stable/installation/installing-binary.html#multi-user-installation"
                echo ""
                echo " --no-daemon: Simple, single-user installation that does not require root and is"
                echo "              trivial to uninstall."
                echo "              (default)"
                echo ""
                echo " --yes:               Run the script non-interactively, accepting all prompts."
                echo ""
                echo " --no-channel-add:    Don't add any channels. nixpkgs-unstable is installed by default."
                echo ""
                echo " --no-modify-profile: Don't modify the user profile to automatically load nix."
                echo ""
                echo " --daemon-user-count: Number of build users to create. Defaults to 32."
                echo ""
                echo " --nix-extra-conf-file: Path to nix.conf to prepend when installing /etc/nix/nix.conf"
                echo ""
                if [ -n "${INVOKED_FROM_INSTALL_IN:-}" ]; then
                    echo " --tarball-url-prefix URL: Base URL to download the Nix tarball from."
                fi
            } >&2

            exit;;
    esac
    shift
done

if [ "$INSTALL_MODE" = "daemon" ]; then
    # Check for bash on systems that don't have it by default
    if [ "$OS" = "FreeBSD" ] && ! command -v bash >/dev/null 2>&1; then
        printf '\e[1;31mError: bash is required for multi-user installation but was not found.\e[0m\n' >&2
        printf 'Please install bash first:\n' >&2
        printf '  pkg install bash\n' >&2
        exit 1
    fi
    printf '\e[1;31mSwitching to the Multi-user Installer\e[0m\n'
    exec "$self/install-multi-user" $ACTION
    exit 0
fi

if [ "$(id -u)" -eq 0 ]; then
    printf '\e[1;31mwarning: installing Nix as root is not supported by this script!\e[0m\n' >&2
fi

echo "performing a single-user installation of Nix..." >&2

if ! [ -e "$dest" ]; then
    cmd="mkdir -m 0755 $dest && chown $USER $dest"
    echo "directory $dest does not exist; creating it by running '$cmd' using $NIX_BECOME" >&2
    if ! $NIX_BECOME sh -c "$cmd"; then
        echo "$0: please manually run '$cmd' as root to create $dest" >&2
        exit 1
    fi
fi

if ! [ -w "$dest" ]; then
    echo "$0: directory $dest exists, but is not writable by you. This could indicate that another user has already performed a single-user installation of Nix on this system. If you wish to enable multi-user support see https://nix.dev/manual/nix/stable/installation/multi-user.html. If you wish to continue with a single-user install for $USER please run 'chown -R $USER $dest' as root." >&2
    exit 1
fi
```

### Command
```bash
INSTALL_SCRIPT=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install" -type f 2>/dev/null | head -1) && grep -n "no-daemon installs" -n "$INSTALL_SCRIPT"
```

### Output
```
66:                printf '\e[1;31mError: --no-daemon installs are no-longer supported on Darwin/macOS!\e[0m\n' >&2
```

### Command
```bash
INSTALL_SCRIPT=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install" -type f 2>/dev/null | head -1) && sed -n '50,90p' "$INSTALL_SCRIPT"
```

### Output
```
        INSTALL_MODE=daemon;;
    *)
        INSTALL_MODE=no-daemon;;
esac

ACTION=

# handle the command line flags
while [ $# -gt 0 ]; do
    case $1 in
        --daemon)
            INSTALL_MODE=daemon
            ACTION=install
            ;;
        --no-daemon)
            if [ "$OS" = "Darwin" ]; then
                printf '\e[1;31mError: --no-daemon installs are no-longer supported on Darwin/macOS!\e[0m\n' >&2
                exit 1
            fi
            INSTALL_MODE=no-daemon
            ACTION=install
            ;;
        --yes)
            export NIX_INSTALLER_YES=1;;
        --no-channel-add)
            export NIX_INSTALLER_NO_CHANNEL_ADD=1;;
        --daemon-user-count)
            export NIX_USER_COUNT="$2"
            shift;;
        --no-modify-profile)
            NIX_INSTALLER_NO_MODIFY_PROFILE=1;;
        --darwin-use-unencrypted-nix-store-volume)
            {
                echo "Warning: the flag --darwin-use-unencrypted-nix-store-volume"
                echo "         is no longer needed and will be removed in the future."
                echo ""
            } >&2;;
        --nix-extra-conf-file)
            # shellcheck disable=SC2155
            export NIX_EXTRA_CONF="$(cat "$2")"
            shift;;
```

### Command
```bash
INSTALL_MULTI=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install-multi-user" -type f 2>/dev/null | head -1) && sed -n '1,200p' "$INSTALL_MULTI"
```

### Output (summarized by tool)
```
I ran `INSTALL_MULTI=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install-multi-user" -type f 2>/dev/null | head -1) && sed -n '1,200p' "$INSTALL_MULTI"` and the command completed with exit code 0 in /Users/harry/tbc-tools.

Key outcomes:
- Located an install-multi-user script at path matching */nix-2.33.3-x86_64-darwin/install-multi-user under /var/folders and printed its first 200 lines.

Critical details:
- Script header and initial content shown (shebang and setup options):
  - Line 1: #!/usr/bin/env bash
  - set -eu
  - set -o pipefail
- Many comments tracing origins of the installer code (sources listed).
- Defined constants and defaults:
  - NIX_USER_COUNT default: 32
  - NIX_BUILD_GROUP_NAME: nixbld
  - NIX_ROOT: /nix
  - PROFILE_TARGETS initialization and related profile configs
  - NIX_INSTALLED_NIX path: /nix/store/yx71gqapmirdpgkn63jakadr7f34fnwk-nix-2.33.3
  - NIX_INSTALLED_CACERT path: /nix/store/7ds4ccbvry9jbfxj7kjnajgn1bxfra4i-nss-cacert-3.115
- Environment and helper functions visible (headless, is_root, OS checks, uninstall_directions, nix_user_for_core, nix_uid_for_core, _textout, header, warningheader, subheader).

Errors and warnings:
- None reported (exit code 0).

Specific data points:
- Script path detected (first match): unknown exact path, but within /var/folders/.../nix-2.33.3-x86_64-darwin/install-multi-user
- NIX_INSTALLED_NIX: "/nix/store/yx71gqapmirdpgkn63jakadr7f34fnwk-nix-2.33.3"
- NIX_INSTALLED_CACERT: "/nix/store/7ds4ccbvry9jbfxj7kjnajgn1bxfra4i-nss-cacert-3.115"
```

### Command
```bash
INSTALL_MULTI=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install-multi-user" -type f 2>/dev/null | head -1) && grep -n "nixbld\|build user\|user count\|NIX_USER_COUNT" "$INSTALL_MULTI" | head -200
```

### Output
```
23:# installer allows overriding build user count to speed up installation
25:readonly NIX_USER_COUNT=${NIX_USER_COUNT:-32}
26:readonly NIX_BUILD_GROUP_NAME="nixbld"
527:    row "     Build Users" "$NIX_USER_COUNT"
534:    subheader "build users:"
537:    for i in $(seq 1 "$NIX_USER_COUNT"); do
575:    task "Setting up the build user $username"
584:It seems the build user $username already exists, but with the UID
616:    if poly_user_note_get "$username" | grep -q "Nix build user $coreid"; then
617:        row "              Note" "Nix build user $coreid"
619:        poly_user_note_set "$username" "Nix build user $coreid"
620:        row "              Note" "Nix build user $coreid"
646:    for i in $(seq 1 "$NIX_USER_COUNT"); do
741:    local -r NIX_UID_RANGES="${NIX_FIRST_BUILD_UID}..$((NIX_FIRST_BUILD_UID + NIX_USER_COUNT - 1))"
```

### Command
```bash
INSTALL_MULTI=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install-multi-user" -type f 2>/dev/null | head -1) && sed -n '520,640p' "$INSTALL_MULTI"
```

### Output
```
    fi
}

setup_report() {
    header "Nix config report"
    row "        Temp Dir" "$SCRATCH"
    row "        Nix Root" "$NIX_ROOT"
    row "     Build Users" "$NIX_USER_COUNT"
    row "  Build Group ID" "$NIX_BUILD_GROUP_ID"
    row "Build Group Name" "$NIX_BUILD_GROUP_NAME"
    if [ "${ALLOW_PREEXISTING_INSTALLATION:-}" != "" ]; then
        row "Preexisting Install" "Allowed"
    fi

    subheader "build users:"

    row "    Username" "UID"
    for i in $(seq 1 "$NIX_USER_COUNT"); do
        row "     $(nix_user_for_core "$i")" "$(nix_uid_for_core "$i")"
    done
    echo ""
}

create_build_group() {
    local primary_group_id

    task "Setting up the build group $NIX_BUILD_GROUP_NAME"
    if ! poly_group_exists "$NIX_BUILD_GROUP_NAME"; then
        poly_create_build_group
        row "            Created" "Yes"
    else
        primary_group_id=$(poly_group_id_get "$NIX_BUILD_GROUP_NAME")
        if [ "$primary_group_id" -ne "$NIX_BUILD_GROUP_ID" ]; then
            failure <<EOF
It seems the build group $NIX_BUILD_GROUP_NAME already exists, but
with the UID $primary_group_id. This script can't really handle
that right now, so I'm going to give up.

You can export NIX_BUILD_GROUP_ID=$primary_group_id and re-run.
EOF
        else
            row "            Exists" "Yes"
        fi
    fi
}

create_build_user_for_core() {
    local coreid
    local username
    local uid

    coreid="$1"
    username=$(nix_user_for_core "$coreid")
    uid=$(nix_uid_for_core "$coreid")

    task "Setting up the build user $username"

    if ! poly_user_exists "$username"; then
        poly_create_build_user "$username" "$uid" "$coreid"
        row "           Created" "Yes"
    else
        actual_uid=$(poly_user_id_get "$username")
        if [ "$actual_uid" != "$uid" ]; then
            failure <<EOF
It seems the build user $username already exists, but with the UID
'$actual_uid'. This script can't really handle that right
now, so I'm going to give up.

If you already created the users and you know they start from
$actual_uid and go up from there, you can edit this script and change
NIX_FIRST_BUILD_UID near the top of the file to $actual_uid and try
again.
EOF
        else
            row "            Exists" "Yes"
        fi
    fi

    if [ "$(poly_user_hidden_get "$username")" = "1" ]; then
        row "            Hidden" "Yes"
    else
        poly_user_hidden_set "$username"
        row "            Hidden" "Yes"
    fi

    if [ "$(poly_user_home_get "$username")" = "/var/empty" ]; then
        row "    Home Directory" "/var/empty"
    else
        poly_user_home_set "$username" "/var/empty"
        row "    Home Directory" "/var/empty"
    fi

    # We use grep instead of an equality check because it is difficult
    # to extract _just_ the user's note, instead it is prefixed with
    # some plist junk. This was causing the user note to always be set,
    # even if there was no reason for it.
    if poly_user_note_get "$username" | grep -q "Nix build user $coreid"; then
        row "              Note" "Nix build user $coreid"
    else
        poly_user_note_set "$username" "Nix build user $coreid"
        row "              Note" "Nix build user $coreid"
    fi

    if [ "$(poly_user_shell_get "$username")" = "/sbin/nologin" ]; then
        row "   Logins Disabled" "Yes"
    else
        poly_user_shell_set "$username" "/sbin/nologin"
        row "   Logins Disabled" "Yes"
    fi

    if poly_user_in_group_check "$username" "$NIX_BUILD_GROUP_NAME"; then
        row "  Member of $NIX_BUILD_GROUP_NAME" "Yes"
    else
        poly_user_in_group_set "$username" "$NIX_BUILD_GROUP_NAME"
        row "  Member of $NIX_BUILD_GROUP_NAME" "Yes"
    fi

    if [ "$(poly_user_primary_group_get "$username")" = "$NIX_BUILD_GROUP_ID"; then
        row "    PrimaryGroupID" "$NIX_BUILD_GROUP_ID"
    else
        poly_user_primary_group_set "$username" "$NIX_BUILD_GROUP_ID"
        row "    PrimaryGroupID" "$NIX_BUILD_GROUP_ID"
    fi
```

### Command
```bash
INSTALL_MULTI=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install-multi-user" -type f 2>/dev/null | head -1) && grep -n "NIX_FIRST_BUILD_UID\|NIX_BUILD_USER_PREFIX\|NIX_BUILD_GROUP_ID" "$INSTALL_MULTI" | head -200
```

### Output
```
28:#   NIX_FIRST_BUILD_UID
29:#   NIX_BUILD_GROUP_ID
168:    echo $((NIX_FIRST_BUILD_UID + $1 - 1))
528:    row "  Build Group ID" "$NIX_BUILD_GROUP_ID"
552:        if [ "$primary_group_id" -ne "$NIX_BUILD_GROUP_ID" ]; then
558:You can export NIX_BUILD_GROUP_ID=$primary_group_id and re-run.
590:NIX_FIRST_BUILD_UID near the top of the file to $actual_uid and try
637:    if [ "$(poly_user_primary_group_get "$username")" = "$NIX_BUILD_GROUP_ID"; then
638:        row "    PrimaryGroupID" "$NIX_BUILD_GROUP_ID"
640:        poly_user_primary_group_set "$username" "$NIX_BUILD_GROUP_ID"
641:        row "    PrimaryGroupID" "$NIX_BUILD_GROUP_ID"
735:    if [ -z "${NIX_FIRST_BUILD_UID+x}" ] || [ -z "${NIX_BUILD_USER_NAME_TEMPLATE+x}" ]; then
741:    local -r NIX_UID_RANGES="${NIX_FIRST_BUILD_UID}..$((NIX_FIRST_BUILD_UID + NIX_USER_COUNT - 1))"
743:    local -r GROUP_TEXT=$(echo -ne "${BLUE}(gid ${NIX_BUILD_GROUP_ID})${ESC}")
761:   NIX_FIRST_BUILD_UID set.
```

### Command
```bash
INSTALL_MULTI=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install-multi-user" -type f 2>/dev/null | head -1) && sed -n '20,60p' "$INSTALL_MULTI"
```

### Output
```
readonly GREEN_UL='\\033[4;32m'
readonly RED='\\033[31m'

# installer allows overriding build user count to speed up installation
# as creating each user takes non-trivial amount of time on macos
readonly NIX_USER_COUNT=${NIX_USER_COUNT:-32}
readonly NIX_BUILD_GROUP_NAME="nixbld"
# each system specific installer must set these:
#   NIX_FIRST_BUILD_UID
#   NIX_BUILD_GROUP_ID
#   NIX_BUILD_USER_NAME_TEMPLATE
# Please don't change this. We don't support it, because the
# default shell profile that comes with Nix doesn't support it.
readonly NIX_ROOT="/nix"
readonly NIX_EXTRA_CONF=${NIX_EXTRA_CONF:-}

# PROFILE_TARGETS will be set later after OS-specific scripts are loaded
PROFILE_TARGETS=()
readonly PROFILE_BACKUP_SUFFIX=".backup-before-nix"
readonly PROFILE_NIX_FILE="$NIX_ROOT/var/nix/profiles/default/etc/profile.d/nix-daemon.sh"

# Fish has different syntax than zsh/bash, treat it separate
readonly PROFILE_FISH_SUFFIX="conf.d/nix.fish"
readonly PROFILE_FISH_PREFIXES=(
    # each of these are common values of $__fish_sysconf_dir,
    # under which Fish will look for a file named
    # $PROFILE_FISH_SUFFIX.
    "/etc/fish"              # standard
    "/usr/local/etc/fish"    # their installer .pkg for macOS
    "/opt/homebrew/etc/fish" # homebrew
    "/opt/local/etc/fish"    # macports
)
readonly PROFILE_NIX_FILE_FISH="$NIX_ROOT/var/nix/profiles/default/etc/profile.d/nix-daemon.fish"

readonly NIX_INSTALLED_NIX="/nix/store/yx71gqapmirdpgkn63jakadr7f34fnwk-nix-2.33.3"
readonly NIX_INSTALLED_CACERT="/nix/store/7ds4ccbvry9jbfxj7kjnajgn1bxfra4i-nss-cacert-3.115"
#readonly NIX_INSTALLED_NIX="/nix/store/j8dbv5w6jl34caywh2ygdy88knx1mdf7-nix-2.3.6"
#readonly NIX_INSTALLED_CACERT="/nix/store/7dxhzymvy330i28ii676fl1pqwcahv2f-nss-cacert-3.49.2"
EXTRACTED_NIX_PATH="$(dirname "$0")"
readonly EXTRACTED_NIX_PATH
```

### Command
```bash
INSTALL_MULTI=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install-multi-user" -type f 2>/dev/null | head -1) && sed -n '700,780p' "$INSTALL_MULTI"
```

### Output
```
        else
            _sudo "to take root ownership of existing Nix store files" \
                  "$get_chr_own" -R "root:$NIX_BUILD_GROUP_NAME" "$NIX_ROOT" || true
        fi
    fi
    _sudo "to make the basic directory structure of Nix (part 1)" \
          install -dv -m 0755 /nix /nix/var /nix/var/log /nix/var/log/nix /nix/var/log/nix/drvs /nix/var/nix{,/db,/gcroots,/profiles,/temproots,/userpool,/daemon-socket} /nix/var/nix/{gcroots,profiles}/per-user

    _sudo "to make the basic directory structure of Nix (part 2)" \
          install -dv -g "$NIX_BUILD_GROUP_NAME" -m 1775 /nix/store

    _sudo "to place the default nix daemon configuration (part 1)" \
          install -dv -m 0555 /etc/nix
}

place_channel_configuration() {
    if [ -z "${NIX_INSTALLER_NO_CHANNEL_ADD:-}" ]; then
        echo "https://channels.nixos.org/nixpkgs-unstable nixpkgs" > "$SCRATCH/.nix-channels"
        _sudo "to set up the default system channel (part 1)" \
            install -m 0644 "$SCRATCH/.nix-channels" "$ROOT_HOME/.nix-channels"
    fi
}

check_selinux() {
    if command -v getenforce > /dev/null 2>&1; then
        if [ "$(getenforce)" = "Enforcing" ]; then
            failure <<EOF
Nix does not work with selinux enabled yet!
see https://github.com/NixOS/nix/issues/2374
EOF
        fi
    fi
}

check_required_system_specific_settings() {
    if [ -z "${NIX_FIRST_BUILD_UID+x}" ] || [ -z "${NIX_BUILD_USER_NAME_TEMPLATE+x}" ]; then
        failure "Internal error: System specific installer for $(uname) ($1) does not export required settings."
    fi
}

welcome_to_nix() {
    local -r NIX_UID_RANGES="${NIX_FIRST_BUILD_UID}..$((NIX_FIRST_BUILD_UID + NIX_USER_COUNT - 1))"
    local -r RANGE_TEXT=$(echo -ne "${BLUE}(uids [${NIX_UID_RANGES}])${ESC}")
    local -r GROUP_TEXT=$(echo -ne "${BLUE}(gid ${NIX_BUILD_GROUP_ID})${ESC}")

    ok "Welcome to the Multi-User Nix Installation"

    cat <<EOF

This installation tool will set up your computer with the Nix package
manager. This will happen in a few stages:

1. Make sure your computer doesn't already have Nix. If it does, I
   will show you instructions on how to clean up your old install.

2. Show you what I am going to install and where. Then I will ask
   if you are ready to continue.

3. Create the system users ${RANGE_TEXT} and groups ${GROUP_TEXT}
   that the Nix daemon uses to run builds. To create system users
   in a different range, exit and run this tool again with
   NIX_FIRST_BUILD_UID set.

4. Perform the basic installation of the Nix files daemon.

5. Configure your shell to import special Nix Profile files, so you
   can use Nix.

6. Start the Nix daemon.

EOF

    if ui_confirm "Would you like to see a more detailed list of what I will do?"; then
        cat <<EOF

I will:

 - make sure your computer doesn't already have Nix files
   (if it does, I will tell you how to clean them up.)
 - create local users (see the list above for the users I'll make)
 - create a local group ($NIX_BUILD_GROUP_NAME)
```

### Command
```bash
INSTALL_DARWIN=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install-darwin-multi-user.sh" -type f 2>/dev/null | head -1) && sed -n '1,200p' "$INSTALL_DARWIN"
```

### Output (summarized by tool)
```
I ran `INSTALL_DARWIN=$(find /var/folders -path "*/nix-2.33.3-x86_64-darwin/install-darwin-multi-user.sh" -type f 2>/dev/null | head -1) && sed -n '1,200p' "$INSTALL_DARWIN"` and got exit code 0 in /Users/harry/tbc-tools.

Key outcomes:
- The script sets default Darwin/Nix-related UID/GID values:
  - NIX_FIRST_BUILD_UID: 351
  - NIX_BUILD_GROUP_ID: 350
  - NIX_BUILD_USER_NAME_TEMPLATE: "_nixbld%d"
- It defines path for nix daemon plist: /Library/LaunchDaemons/org.nixos.nix-daemon.plist
- It computes/uses a volume create flag: NIX_VOLUME_CREATE defaults to 1
- It contains readiness checks and functions for:
  - read_only_root detection
  - poly_cure_artifacts (volume cleanup before install)
  - poly_service_setup_note and related install/uninstall guidance
  - DSCL helpers and functions to create hidden user/build group
- It sources/create-a-darwin-volume script: creates a volume for Nix if needed
- It includes logic to install and enable nix-daemon LaunchDaemon and to start it

Critical details (verbatim if present in output):
- UID/GID defaults:
  - export NIX_FIRST_BUILD_UID="${NIX_FIRST_BUILD_UID:-351}"
  - export NIX_BUILD_GROUP_ID="${NIX_BUILD_GROUP_ID:-350}"
  - export NIX_BUILD_USER_NAME_TEMPLATE="_nixbld%d"

- Daemon plist path:
  - readonly NIX_DAEMON_DEST=/Library/LaunchDaemons/org.nixos.nix-daemon.plist

- Volume creation flag:
  - readonly NIX_VOLUME_CREATE=${NIX_VOLUME_CREATE:-1}

- User/group related helper points:
  - poly_group_exists, poly_group_id_get
  - poly_create_build_group uses: /usr/sbin/dseditgroup -o create ... -i "$NIX_BUILD_GROUP_ID" "$NIX_BUILD_GROUP_NAME"
  - poly_user_exists, poly_user_id_get
  - dscl_create has logic to retry on eNotYetImplemented

- Notes indicating behavior/steps:
  - "Setting up the nix-daemon LaunchDaemon"
  - "to load the LaunchDaemon plist for nix-daemon"
  - "to start the nix-daemon"
```

## Files Modified (Iteration 2)
- `.github/workflows/build_macos_tools.yml` (removed `install_options: --no-daemon`; added env overrides: `NIX_FIRST_BUILD_UID`, `NIX_BUILD_USER_NAME_TEMPLATE`)
