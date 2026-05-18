# Security Policy

## Supported Versions

| Version | Supported |
|---|---|
| 0.1.x (Alpha) | Yes — active development |
| < 0.1.0 | No |

## Reporting a Vulnerability

AuraShell interacts with Windows shell internals (DLL injection, registry modification, named pipe IPC, optional elevated service). Security issues are taken seriously.

**Please do not open a public GitHub issue for security vulnerabilities.**

### How to report

Email **iajensen@icloud.com** with:
1. A description of the vulnerability
2. Steps to reproduce it
3. The potential impact (what an attacker could do)
4. Your name/handle (for credit in the changelog, if desired)

### What to expect

- **Acknowledgement**: Within 48 hours
- **Status update**: Within 7 days (confirmed, investigating, or won't fix with explanation)
- **Fix timeline**: Critical issues targeting within 14 days; moderate within 30 days

### Scope

Vulnerabilities of particular interest given AuraShell's architecture:
- DLL injection into processes other than the intended target
- Privilege escalation via the named pipe IPC or the optional admin service
- Path traversal in theme or icon file loading
- Registry key ACL weaknesses that allow non-admin users to escalate

### Out of scope

- Issues requiring physical access to the machine
- Social engineering attacks
- Vulnerabilities in third-party dependencies (report those upstream; notify us if they affect AuraShell)
