Manual upload steps and troubleshooting

1. Ensure Git is installed on your machine:
   - Download and install: https://git-scm.com/download/win
   - Verify with: `git --version`

2. Initialize and commit locally (run in project root):
   ```powershell
   git init
   git add -A
   git commit -m "Initial commit"
   ```

3. Create a repository on GitHub (via website or `gh` CLI) and copy the remote URL.

4. Add remote and push:
   ```powershell
   git remote add origin https://github.com/USERNAME/REPO.git
   git branch -M main
   git push -u origin main
   ```

Troubleshooting
- If `git` prints errors like "BUG (fork bomb): ..." when run, reinstall Git for Windows and restart your machine.
- Alternatively use GitHub Desktop or Visual Studio Code which bundle Git functionality.
- For large files (>100MB) use Git LFS: `git lfs install` and track large file types.
