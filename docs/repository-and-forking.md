# Repository Information & Forking Guide

> Based on SVN metadata inspected from the local working copy at r26369.

---

## Upstream Repository

| Property | Value |
|---|---|
| **Hosting** | SourceForge (SVN) |
| **Project page** | https://sourceforge.net/projects/gwyddion/ |
| **Web site** | http://gwyddion.net/ |
| **SVN repository root** | `https://svn.code.sf.net/p/gwyddion/code` |
| **Trunk URL** | `https://svn.code.sf.net/p/gwyddion/code/trunk` |
| **gwyddion subdir** | `https://svn.code.sf.net/p/gwyddion/code/trunk/gwyddion` |
| **Online browser** | https://sourceforge.net/p/gwyddion/code/HEAD/tree/ |
| **Repository UUID** | `25697fe4-d644-49b8-9d30-be5465f2adf3` |
| **Local revision** | r26369 (last changed 2024-05-30 by `yeti-dn`) |
| **Bug reports** | klapetek@gwyddion.net |
| **Mailing lists** | gwyddion-users@lists.sourceforge.net / gwyddion-devel@lists.sourceforge.net |

---

## Forking Options

### Option 1: Mirror to GitHub via git-svn (Recommended)

Converts the full SVN history to Git and hosts on GitHub.

```bash
# Install git-svn
sudo apt install git-svn

# Clone SVN trunk as a Git repo (full history preserved)
git svn clone https://svn.code.sf.net/p/gwyddion/code/trunk \
    --no-metadata \
    gwyddion-fork

cd gwyddion-fork

# Push to your GitHub repo
git remote add origin https://github.com/YOUR_USERNAME/gwyddion-fork.git
git push -u origin main
```

**Staying in sync with upstream:**
```bash
git svn fetch
git rebase remotes/git-svn
git push origin main
```

---

### Option 2: Fork on SourceForge

1. Create a SourceForge account at https://sourceforge.net
2. Go to https://sourceforge.net/projects/gwyddion/
3. Click **Fork** — SourceForge copies the SVN repo under your account
4. Check out your fork:
```bash
svn checkout https://svn.code.sf.net/p/YOUR_SF_USERNAME/gwyddion-fork/trunk
```

---

### Option 3: Convert Local Working Copy to Git (Fastest)

```bash
cd /path/to/gwyddion-code

git init
git add .
git commit -m "Initial commit: Gwyddion SVN r26369 (2024-05-30)"

git remote add origin https://github.com/YOUR_USERNAME/gwyddion-fork.git
git push -u origin main
```

> Note: This loses per-file SVN commit history but is the fastest path to a Git-based fork.

---

## Comparison

| Option | SVN history | Sync with upstream | Git-based | Effort |
|---|:---:|:---:|:---:|---|
| git-svn → GitHub | ✅ Full | ✅ Via `git svn fetch` | ✅ | Medium |
| SourceForge fork | ✅ Full | ✅ Via SVN merge | ❌ SVN only | Low |
| Local → new Git repo | ❌ Single commit | ⚠️ Manual `svn update` + re-commit | ✅ | Very low |

---

## Updating the Local Working Copy

Since this workspace is a live SVN checkout, to pull the latest upstream changes:

```bash
svn update /path/to/gwyddion-code
```
