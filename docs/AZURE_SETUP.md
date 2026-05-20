# Azure Trusted Signing — Phase 1 Setup Runbook

**Date created:** 2026-05-19  
**Author:** Claude (AI agent)  
**Status:** ACTION REQUIRED — run through each step in order  
**Related:** See [SIGNING_STRATEGY.md](./SIGNING_STRATEGY.md) for the decision rationale.

---

## Before you start

Run through the status check in `SIGNING_STRATEGY.md` first. If any resource already exists,
skip that step — **do not re-create**. Trusted Signing accounts and certificate profiles
cannot be renamed once provisioned.

**Time estimate:** ~30 minutes of CLI work. Identity validation takes 1-3 business days after
submission (automated for Individual validation, manual review for Organization).

---

## Prerequisites

### 1. Install Azure CLI (if not already installed)

```powershell
# Windows — recommended via winget
winget install --id Microsoft.AzureCLI --source winget
az version   # verify: should print 2.x or later
```

### 2. Log in to Azure

```powershell
az login
# A browser window opens — sign in with the account linked to your Azure subscription.
# After login, verify the correct subscription is active:
az account show --query "{subscription:name, id:id}" -o table
```

### 3. Install the Trusted Signing CLI extension

```powershell
az extension add --name trustedsigning
az extension show --name trustedsigning   # verify install
```

---

## Step 1: Create a Resource Group

```powershell
az group create \
  --name   "AuraShellSigning" \
  --location "eastus"
```

> **Why eastus?** Trusted Signing is available in eastus and westus2. Pick the closest to you.
> Re-run `az group list -o table` to confirm it was created.

---

## Step 2: Create a Trusted Signing Account

```powershell
az trustedsigning account create \
  --name                "AuraShellSigning" \
  --resource-group      "AuraShellSigning" \
  --location            "eastus" \
  --sku                 "Basic"
```

**SKU options:**
- `Basic` — $9.99/month, 1000 signatures/month. Sufficient for v0.1.0-alpha.
- `Premium` — $99/month, 10 000 signatures/month. Use when you need CI to sign every commit.

Save the output JSON — you'll need the `id` field later.

---

## Step 3: Submit Identity Validation

This step happens in the **Azure Portal** (no CLI support yet as of 2026-05).

1. Go to portal.azure.com → search "Trusted Signing" → select your new account "AuraShellSigning"
2. Click **Identity Validation** in the left menu → **+ Add**
3. Choose **Individual** validation (you are a solo developer)
   - Organization validation is for businesses with DUNS numbers
4. Fill in the form:
   - **First name / Last name:** your real name (must match a government-issued ID)
   - **Email:** iajensen@icloud.com
   - **Country/region:** your country
5. Submit and wait for an email from Microsoft confirming validation
   - **Individual:** typically approved automatically within hours
   - **Organization:** manual review, 1-3 business days

**Check status at any time:**
```powershell
az trustedsigning account show \
  --name           "AuraShellSigning" \
  --resource-group "AuraShellSigning" \
  --query          "properties.accountUri"
```

---

## Step 4: Create a Certificate Profile (after identity is validated)

Only do this step after the Identity Validation status in the Portal shows **Completed**.

```powershell
az trustedsigning certificate-profile create \
  --account-name        "AuraShellSigning" \
  --resource-group      "AuraShellSigning" \
  --profile-name        "AuraShellProfile" \
  --profile-type        "PublicTrust"
```

`PublicTrust` creates a certificate that browsers and Windows Defender will trust globally.
`PrivateTrust` is for internal/enterprise use only.

---

## Step 5: Create a Service Principal for CI signing

```powershell
# Create the service principal
az ad sp create-for-rbac \
  --name    "AuraShellCISigning" \
  --role    "Trusted Signing Certificate Profile Signer" \
  --scopes  "/subscriptions/$(az account show --query id -o tsv)/resourceGroups/AuraShellSigning/providers/Microsoft.CodeSigning/codeSigningAccounts/AuraShellSigning" \
  --sdk-auth
```

Save the JSON output — it contains `clientId`, `clientSecret`, and `tenantId`.

---

## Step 6: Add Secrets to GitHub Actions

In your browser:  
`https://github.com/Iajensen222222/AuraShell/settings/secrets/actions`

Create these 5 secrets (values from the service principal JSON above):

| Secret name | Value |
|---|---|
| `AZURE_TENANT_ID` | `tenantId` from SP JSON |
| `AZURE_CLIENT_ID` | `clientId` from SP JSON |
| `AZURE_CLIENT_SECRET` | `clientSecret` from SP JSON |
| `TRUSTED_SIGNING_ACCOUNT_NAME` | `AuraShellSigning` |
| `TRUSTED_SIGNING_PROFILE_NAME` | `AuraShellProfile` |

---

## Step 7: Test local signing

With the Azure CLI still logged in:

```powershell
# From the AuraShell Distribution/code-signing/ directory:
.\sign-binaries.ps1 `
  -BuildDir              "..\..\AuraShell\out\build\x64-Release\bin" `
  -TrustedSigningAccount "AuraShellSigning" `
  -CertificateProfile    "AuraShellProfile" `
  -Endpoint              "https://eus.codesigning.azure.net"
```

Then verify:
```powershell
signtool.exe verify /pa /v "..\..\AuraShell\out\build\x64-Release\bin\AuraConfig.exe"
```

A valid signature produces: `Successfully verified: AuraConfig.exe`

---

## Step 8: Enable CI signing for releases

The `release-signing-snippet.yml` in this directory is ready to paste into a new GitHub Actions
workflow (or append to `.github/workflows/ci.yml`). It triggers on `v*.*.*` tag pushes and:

1. Builds Release configuration
2. Signs all .exe and .dll files via `azure/trusted-signing-action@v0.5.1`
3. Signs the InnoSetup installer
4. Verifies signatures
5. Uploads signed artifacts to the GitHub Release

Copy it to `.github/workflows/release.yml` to activate CI signing.

---

## Troubleshooting

| Error | Fix |
|---|---|
| `The subscription is not registered for Trusted Signing` | `az provider register --namespace Microsoft.CodeSigning` then wait ~2 minutes |
| `Identity validation not completed` | Check portal for pending validation step; resubmit if rejected |
| `Insufficient permissions` | Ensure the service principal has the `Trusted Signing Certificate Profile Signer` role, not just `Contributor` |
| `AADSTS70011: Invalid scope` | Re-run the SP creation command with the exact resource scope path |

---

## Estimated monthly cost

| Tier | Monthly | Signatures |
|---|---|---|
| Basic | $9.99 | 1 000/month |
| Premium | $99.00 | 10 000/month |

For a solo dev releasing once a month: **Basic** is sufficient.
For CI signing on every PR: **Premium**.

You can upgrade the SKU at any time without re-doing identity validation.
