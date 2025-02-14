#!/usr/bin/env bash

# --- Configuration ---
CVS_WORKING_DIR="/path/to/your/cvs/working/copy"  # *** MUST BE CONFIGURED ***
GITHUB_REPO="fermi-ad/acnetd"
CVS_COMMIT_MESSAGE="Sync from Git - Release (automated script)"
CVS_TAG_PREFIX="git-hash-"

# --- Script Logic ---

# 1. Ensure we are in the CVS working directory
cd "$CVS_WORKING_DIR" || { echo "Error: Could not change directory to CVS working copy: $CVS_WORKING_DIR"; exit 1; }

# 2. Get the latest GitHub Release using curl and GitHub API (simplified - get latest release directly)
GITHUB_API_URL="https://api.github.com/repos/$GITHUB_REPO/releases"

# Get releases, sort by created_at (descending), get the first one (latest)
LATEST_RELEASE_JSON=$(curl -s "${GITHUB_API_URL}" | jq -s "sort_by(.created_at) | reverse | .[0]")

if [ -z "$LATEST_RELEASE_JSON" ] || [[ "$LATEST_RELEASE_JSON" == "null" ]]; then
    echo "Error: No GitHub Releases found in repository: $GITHUB_REPO"
    exit 1
fi

RELEASE_NAME=$(echo "$LATEST_RELEASE_JSON" | jq -r '.name')
RELEASE_TAG_NAME=$(echo "$LATEST_RELEASE_JSON" | jq -r '.tag_name') # Still get tag name for logging
PATCH_ASSET_URL=$(echo "$LATEST_RELEASE_JSON" | jq -r '.assets[0].browser_download_url') # Use browser_download_url for direct download
GIT_COMMIT_SHA=$(echo "$LATEST_RELEASE_JSON" | jq -r '.target_commitish') # <--- Get commit SHA directly from API response

if [ -z "$PATCH_ASSET_URL" ]; then
    echo "Error: No patch file asset found in the latest GitHub Release: $RELEASE_NAME"
    exit 1
fi

PATCH_FILE_NAME=$(basename "$PATCH_ASSET_URL")
PATCH_FILE_LOCAL="./$PATCH_FILE_NAME"


# 3. Download the patch file using curl (same as before)
echo "Downloading patch file: $PATCH_FILE_NAME from Release: $RELEASE_NAME ($RELEASE_TAG_NAME) - Commit: $GIT_COMMIT_SHA"
curl -L -o "$PATCH_FILE_LOCAL" "$PATCH_ASSET_URL" # -L to follow redirects, -o for output file

if [ ! -f "$PATCH_FILE_LOCAL" ]; then
    echo "Error: Failed to download patch file: $PATCH_FILE_NAME"
    exit 1
fi


# 4. Apply the patch (same as before)
echo "Applying patch: $PATCH_FILE_NAME"
if patch -p1 < "$PATCH_FILE_LOCAL"; then
    echo "Patch applied successfully."
else
    echo "Error: Patch application failed! Manual conflict resolution required."
    echo "Please resolve conflicts in your CVS working copy, then re-run this script."
    exit 1 # Exit script - manual intervention needed
fi


# 5. CVS Commit (same as before)
echo "Committing changes to CVS..."
cvs commit -m "$CVS_COMMIT_MESSAGE - Release: $RELEASE_NAME ($RELEASE_TAG_NAME) - Git Commit: $GIT_COMMIT_SHA" .
if [ $? -eq 0 ]; then
    echo "CVS Commit successful."
else
    echo "Error: CVS Commit failed!"
    exit 1
fi

# 6. CVS Tagging (Now using the Commit SHA from API)
CVS_TAG_NAME="${CVS_TAG_PREFIX}${GIT_COMMIT_SHA}"
echo "Tagging CVS with tag: $CVS_TAG_NAME"
cvs tag "$CVS_TAG_NAME"
if [ $? -eq 0 ]; then
    echo "CVS Tagging successful."
else
    echo "Error: CVS Tagging failed!"
    exit 1
fi


echo "CVS Sync Patch process completed for Release: $RELEASE_NAME ($RELEASE_TAG_NAME) - Git Commit: $GIT_COMMIT_SHA"
exit 0 # Exit script - explicit success
