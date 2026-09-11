# Publishing Outlast 2 VR on GitHub

This repository is the public source tree. Do not upload the developer workspace,
an installed game directory, or any original Outlast 2 files.

## 1. Create the GitHub repository

1. Sign in to GitHub and select **New repository**.
2. Choose your account as the owner.
3. Use `Outlast2VR` as the repository name.
4. Suggested description: `Unofficial open-source PC VR conversion for Outlast 2 using OpenXR.`
5. Select **Public**.
6. Do not add a README, `.gitignore`, or license on GitHub. This source tree already
   contains all three.
7. Select **Create repository**.

## 2. Make the first source commit

The repository has already been initialized on the `main` branch and its public
source files are staged. Set the author information you want shown publicly, then
create the first commit:

```powershell
git config user.name "YOUR PUBLIC NAME"
git config user.email "YOUR PUBLIC OR GITHUB NOREPLY EMAIL"
git commit -m "Release Outlast 2 VR Public Beta 1 source"
```

These settings apply only to this repository. Use GitHub's no-reply address if you
do not want a personal email address recorded in the public commit.

## 3. Push this local source tree

GitHub will show a repository URL after creation. From this folder, run:

```powershell
git remote add origin https://github.com/YOUR-ACCOUNT/Outlast2VR.git
git push -u origin main
```

Replace `YOUR-ACCOUNT` with the GitHub account or organization that owns the repo.
If Git asks you to authenticate, use the browser sign-in flow or a personal access
token; GitHub account passwords are not accepted for Git operations over HTTPS.

## 4. Check the public repository

Before announcing it:

- Confirm that the README appears on the front page.
- Open the **Actions** tab and confirm **Windows build and tests** passes.
- Confirm that no `.exe`, `.dll`, game package, save data, log, crash dump, or
  original Outlast 2 asset is present in the source history.
- Keep **Issues** enabled so beta testers can report reproducible problems.

## 5. Create Public Beta 1

1. Open **Releases** and select **Draft a new release**.
2. Create tag `v0.1.0-beta.1` from `main`.
3. Use the title `Outlast 2 VR — Public Beta 1`.
4. Paste the contents of `RELEASE_NOTES.md` into the description.
5. Mark the release as a **pre-release**.
6. Attach these two files from `release-assets`:
   - `Outlast2VR-Beta1-Windows-x64.zip`
   - `Outlast2VR-Beta1-Windows-x64.sha256.txt`
7. Save as a draft first, inspect the page and attachments, then publish.

GitHub automatically provides source-code archives. The attached Windows ZIP is the
ready-to-install build; users should not download GitHub's source archive when they
only want to play.

## 6. After publishing

- Test the release ZIP from the public release page on a clean copy of the game's
  `Binaries\Win64` directory.
- Put the release URL in the trailer/video description.
- Ask reports to include headset, OpenXR runtime, connection method, GPU, the exact
  location in the game, reproduction steps, and `outlast2_vr_p34.log`.
- Never tell users to redistribute `Outlast2.exe` or any game content.

For future versions, update `RELEASE_NOTES.md`, run `scripts\Package-Release.ps1`,
create a new semantic version tag, and attach the newly generated release ZIP and
checksum file.
