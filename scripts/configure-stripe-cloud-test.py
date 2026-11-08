"""Run on the PULSO host. Uses server secrets; never prints credentials.

Only provisions Stripe TEST objects. Does not restart services or charge cards.
"""
import datetime
import json
import os
from pathlib import Path
import subprocess
import urllib.parse
import urllib.request

container = json.loads(subprocess.check_output(['docker', 'inspect', 'pulso-app-1']))[0]
env = dict(item.split('=', 1) for item in container['Config']['Env'])
key = env['STRIPE_SECRET_KEY']
assert key.startswith('sk_test_'), 'Refusing to provision a live account'
public_url = 'https://pulso-audio.com'

def stripe(path, fields=None, idempotency=None):
    headers = {'Authorization': 'Bearer ' + key}
    if idempotency:
        headers['Idempotency-Key'] = idempotency
    data = urllib.parse.urlencode(fields).encode() if fields is not None else None
    req = urllib.request.Request('https://api.stripe.com/v1/' + path, data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=30) as response:
        return json.load(response)

lookup = 'pulso_cloud_basic_monthly_14usd_v1'
prices = stripe('prices?' + urllib.parse.urlencode({'lookup_keys[]': lookup}))['data']
if prices:
    price = prices[0]
else:
    product = stripe('products', {'name': 'PULSO Cloud Basic',
        'metadata[pulso_plan]': 'cloud_monthly'}, 'pulso-cloud-basic-product-v1')
    price = stripe('prices', {'product': product['id'], 'currency': 'usd',
        'unit_amount': 1400, 'recurring[interval]': 'month', 'lookup_key': lookup},
        'pulso-cloud-basic-monthly-price-v1')
assert price['active'] and price['unit_amount'] == 1400 and price['currency'] == 'usd'
assert price['recurring']['interval'] == 'month' and price['recurring']['interval_count'] == 1

portal = stripe('billing_portal/configurations?limit=100')['data']
if not any(item['active'] and item.get('is_default') for item in portal):
    stripe('billing_portal/configurations', {
        'business_profile[headline]': 'PULSO Cloud',
        'features[customer_update][enabled]': 'true',
        'features[customer_update][allowed_updates][]': 'email',
        'features[invoice_history][enabled]': 'true',
        'features[payment_method_update][enabled]': 'true',
        'features[subscription_cancel][enabled]': 'true',
        'features[subscription_cancel][mode]': 'at_period_end',
        'default_return_url': public_url + '/account',
    }, 'pulso-cloud-test-portal-v1')

# Verify that the existing Checkout tax/address options are accepted by Stripe.
customer = stripe('customers', {'description': 'PULSO test checkout configuration probe'})
try:
    session = stripe('checkout/sessions', {'customer': customer['id'], 'mode': 'subscription',
        'line_items[0][price]': price['id'], 'line_items[0][quantity]': 1,
        'automatic_tax[enabled]': 'true', 'billing_address_collection': 'required',
        'tax_id_collection[enabled]': 'true', 'customer_update[address]': 'auto',
        'customer_update[name]': 'auto',
        'success_url': public_url + '/account?checkout=success',
        'cancel_url': public_url + '/pricing?checkout=canceled'})
    stripe('checkout/sessions/' + session['id'] + '/expire', {})
finally:
    request = urllib.request.Request('https://api.stripe.com/v1/customers/' + customer['id'],
        method='DELETE', headers={'Authorization': 'Bearer ' + key})
    urllib.request.urlopen(request, timeout=30).close()

# Use a direct canonical webhook URL; redirects fail Stripe delivery.
endpoints = stripe('webhook_endpoints?limit=100')['data']
matching = [endpoint for endpoint in endpoints if endpoint['url'] in (
    public_url + '/api/billing/webhook',
    'https://pulso.cargo-core.com/api/billing/webhook')]
assert len(matching) == 1, 'Expected one PULSO webhook; inspect before changing anything'
endpoint = matching[0]
assert endpoint['status'] == 'enabled' and not endpoint['livemode']
stripe('webhook_endpoints/' + endpoint['id'], {'url': public_url + '/api/billing/webhook'})
for portal_config in portal:
    if portal_config['active'] and portal_config.get('is_default'):
        stripe('billing_portal/configurations/' + portal_config['id'],
               {'default_return_url': public_url + '/account'})

path = Path('/www/Pulso/.env')
before = path.read_text()
updates = {'STRIPE_MONTHLY_PRICE_ID': price['id'], 'PULSO_CREDITS_ENABLED': 'true',
           'PUBLIC_URL': public_url}
lines = before.splitlines()
for name, value in updates.items():
    lines = [line for line in lines if not line.startswith(name + '=')]
    lines.append(name + '=' + value)
backup = path.with_name('.env.before-cloud-billing-' + datetime.datetime.now().strftime('%Y%m%d%H%M%S'))
backup.write_text(before)
os.chmod(backup, 0o600)
temporary = path.with_name('.env.billing-next')
temporary.write_text('\n'.join(lines) + '\n')
os.chmod(temporary, 0o600)
os.replace(temporary, path)
print(json.dumps({'mode': 'test', 'monthlyPrice': price['id'], 'amountUsd': 14,
    'monthlyCredits': 20, 'creditsPerWork': 3, 'checkoutProbe': 'passed',
    'configuration': 'saved; requires app restart'}))
