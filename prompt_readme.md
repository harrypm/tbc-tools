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
