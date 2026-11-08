# Cloud billing on the public site: test mode

Public site: https://pulso-audio.com/pricing

Stripe is in test mode. No real charges are enabled. Cloud Basic is USD 14
per month, granting 20 credits per paid monthly invoice. A completed work
uses 3 credits; existing demo/owner exemptions remain unchanged.

## Verified end-to-end

- Authenticated Checkout from the public API creates a test session.
- A Stripe test subscription emits actual Stripe webhook notifications.
- The application activates Cloud and grants exactly 20 credits.
- The invoice has one credit-ledger entry.
- The authenticated billing portal opens.
- The probe cancels its subscription, deletes its Stripe customer, and removes
  its temporary login session. Its internal audit account remains.

The old cargo-core hostname redirects to pulso-audio.com. Stripe must target
https://pulso-audio.com/api/billing/webhook directly, without a redirect.
PUBLIC_URL and the portal return URL use the same canonical hostname.
Changing the existing webhook URL preserves its signing secret.

## Manual browser check

Sign in with a verified account without an active subscription. On Pricing,
choose “Probar suscripción”. Use Stripe's test card 4242 4242 4242 4242,
a future expiration date and any valid three-digit CVC. Never use a real card.
After returning to Account, confirm the subscription and the 20-credit grant.
The owner account keeps unlimited generation regardless of its credit balance.

## Tools and precautions

`scripts/configure-stripe-cloud-test.py` configures test objects and backs up
the server environment privately. Applying environment changes requires an
app restart; first check that there are no queued/running compositions.

`scripts/smoke-stripe-cloud-test.py` exercises the public API and actual
Stripe test webhooks using a separate disabled-login test account. It never
generates music or moves real money. Both scripts reject live Stripe keys.

This is not approval for real-money launch. Live payments require an eligible,
activated Stripe account, live prices and credentials, the live webhook secret,
and a separate live-mode readiness review.
