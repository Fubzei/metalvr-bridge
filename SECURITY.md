# Security Policy

## Supported Branches

| Branch | Support Status |
|--------|----------------|
| `main` | Supported |
| active pull request branches | Best effort |
| older feature branches | Not supported |

Security fixes are tracked against `main`. If a fix is developed on a pull request
branch first, it should be merged into `main` as soon as practical.

## Reporting a Vulnerability

If you find a security issue, please use GitHub's private vulnerability reporting
flow for this repository when available.

If private reporting is unavailable for any reason, open a draft GitHub security
advisory or contact the maintainer directly instead of filing a public issue.

Please include:

- a clear description of the issue
- affected file paths or components
- reproduction steps or proof of concept
- impact assessment
- any suggested mitigation

Please do **not** include live secrets, access tokens, or private credentials in
the report.

## Scope Notes

This project includes:

- native C++ and Objective-C++ code
- GitHub Actions workflows
- a Swift launcher app
- a Vulkan ICD manifest and Metal integration

Please report issues in any of those areas, including:

- memory safety bugs
- command injection or unsafe process launches
- insecure workflow or secret handling
- unsafe manifest or library loading behavior
- dependency or supply-chain concerns
