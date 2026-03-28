# MetalVR Bridge — GitHub Repo Setup Guide

## Step-by-Step (Windows)

### 1. Install Git (if you don't have it)
Download from: https://git-scm.com/download/win
During install, keep all defaults. This gives you Git Bash.

### 2. Create a GitHub account (if you don't have one)
Go to: https://github.com
Sign up, verify email.

### 3. Create the repo on GitHub
- Click the green "New" button (top left)
- Repository name: `metalvr-bridge`
- Description: `Vulkan-to-Metal translation layer — run Windows games on Mac`
- Set to **Public** (so GitHub Actions macOS runners are free)
- Check "Add a README file"
- Add .gitignore: select "C++"
- License: MIT
- Click "Create repository"

### 4. Clone it to your PC
Open Git Bash (right-click desktop -> Git Bash Here), then:

```bash
cd "/c/Users/Lbfal/OneDrive/Documents/Claude/Projects"
git clone https://github.com/YOUR_USERNAME/metalvr-bridge.git
```

### 5. Copy your existing code into the cloned folder
Copy everything from your current project folder into the new `metalvr-bridge` folder.
The folder structure should match what's in the STRUCTURE section below.

### 6. Push it up
```bash
cd metalvr-bridge
git add .
git commit -m "Initial commit: Milestones 0-9 complete, launcher app"
git push origin main
```

### 7. Give your buddy access
- Go to your repo on GitHub
- Settings -> Collaborators -> Add people
- Add your buddy's GitHub username
- He can now clone the repo on his Mac and build it

### 8. Your buddy clones and builds on Mac
```bash
git clone https://github.com/YOUR_USERNAME/metalvr-bridge.git
cd metalvr-bridge/launcher
bash setup.sh
```
