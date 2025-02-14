# ACNETD Service

This project implements the ACNET daemon (`acnetd`) and is _the_ implementation of the ACNET communication protocol.

## Overview

The ACNETD service is responsible for managing UDP connections on the local machine and handling various tasks related to network communication. It includes classes and methods for handling external and internal tasks, managing IP addresses, and processing network commands.

## Building and Running

To build and run the ACNETD service, follow these steps:

1. Ensure you have the necessary dependencies installed.
2. Compile the source code using your preferred C++ compiler.
3. Run the resulting executable.

### Prerequisites

- g++
- make
- Standard build tools (ar, ranlib)
- OpenSSL development libraries (`-I/usr/include/openssl`). If you do not have OpenSSL installed, you can follow the instructions [here](https://www.openssl.org/source/) to download and install it.

### Compilation

To compile the ACNETD service, navigate to the project directory and run:

```sh
make
```

This will generate the `acnetd` executable in the project directory.

## CVS Synchronization Process using Git Patches

Due to our build system's reliance on CVS, we need to bridge the gap between our Git-based development workflow and CVS. This process automates the generation of patch files for each commit to the `main` branch in Git and provides a bash script to assist with the manual application of these patches to our CVS repository.

### Prerequisites on the CVS Server

Before you can use the CVS Sync Patch script, ensure the following tools are installed on your CVS server (or the node you use to access CVS):

- **`curl`**:  For downloading patch files from GitHub Releases. (Confirmed: `curl 7.76.1` is sufficient)
- **`jq`**: For parsing JSON output from the GitHub API. (Confirmed: `jq 1.6` is sufficient)
- **`patch`**:  For applying the generated patch files to your CVS working copy.
- **`cvs`**:  The CVS client to interact with your CVS repository.

### GitHub Workflow Setup

The automated part of this process is handled by a GitHub Workflow defined in `.github/workflows/cvs-patch.yaml`.
