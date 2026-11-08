"""End-to-end TEST subscription probe, using an isolated disabled-login account.
Creates no music and moves no real money. Run after deploying the test config.
"""
import hashlib
import json
import secrets
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

config = json.loads(subprocess.check_output(['docker', 'inspect', 'pulso-app-1']))[0]
env = dict(item.split('=', 1) for item in config['Config']['Env'])
assert env['STRIPE_SECRET_KEY'].startswith('sk_test_')
assert env.get('PULSO_CREDITS_ENABLED') == 'true'
public_url = env['PUBLIC_URL'].rstrip('/')
uid, token = str(uuid.uuid4()), secrets.token_hex(32)
subscription = None
customer = None
def sql(query):
    return subprocess.check_output(['docker', 'exec', 'pulso-postgres-1', 'psql',
        '-v', 'ON_ERROR_STOP=1', '-U', 'pulso', '-d', 'pulso', '-Atc', query], text=True).strip()
def stripe(path, fields=None, method=None):
    data = urllib.parse.urlencode(fields).encode() if fields is not None else None
    request = urllib.request.Request('https://api.stripe.com/v1/' + path,
        data=data, method=method, headers={'Authorization': 'Bearer ' + env['STRIPE_SECRET_KEY']})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)
def api(path, body=None):
    request = urllib.request.Request(public_url + '/api/' + path,
        data=json.dumps(body).encode() if body is not None else None,
        headers={'Cookie': 'pulso_session=' + token, 'Content-Type': 'application/json',
                 'Origin': public_url})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)
try:
    now = int(time.time())
    sql(f"INSERT INTO users(id,email,display_name,password_hash,email_verified,created_at) VALUES('{uid}','billing-{uid}@pulso.invalid','Internal billing test','!disabled',TRUE,{now}); INSERT INTO sessions(token_hash,user_id,expires_at,created_at) VALUES('{hashlib.sha256(token.encode()).hexdigest()}','{uid}',{now+600},{now});")
    checkout = api('billing/checkout', {'plan': 'cloud_monthly'})
    assert checkout['url'].startswith('https://checkout.stripe.com/')
    customer = sql(f"SELECT stripe_customer_id FROM users WHERE id='{uid}'")
    sessions = stripe('checkout/sessions?' + urllib.parse.urlencode({'customer': customer}))['data']
    assert len(sessions) == 1 and sessions[0]['livemode'] is False
    stripe('checkout/sessions/' + sessions[0]['id'] + '/expire', {})
    print('PASS: authenticated application Checkout returns a test Stripe session', flush=True)
    # Use Stripe's documented test payment method, not browser/card automation.
    payment_method = stripe('payment_methods/pm_card_visa/attach', {'customer': customer})
    subscription = stripe('subscriptions', {'customer': customer,
        'items[0][price]': env['STRIPE_MONTHLY_PRICE_ID'],
        'default_payment_method': payment_method['id'],
        'metadata[pulso_user_id]': uid, 'metadata[pulso_plan]': 'cloud_monthly'})
    assert subscription['status'] == 'active' and subscription['livemode'] is False
    for attempt in range(30):
        row = sql(f"SELECT COALESCE((SELECT available FROM credit_wallets WHERE user_id='{uid}'),0),COALESCE((SELECT status FROM subscriptions WHERE user_id='{uid}'),'none')")
        if row == '20|active':
            break
        time.sleep(2)
    assert row == '20|active', 'Real Stripe webhook did not grant 20 credits and activate test subscription: ' + row
    invoice = subscription['latest_invoice']
    count = sql(f"SELECT count(*) FROM credit_ledger WHERE user_id='{uid}' AND reference='subscription_invoice:{invoice}'")
    assert count == '1'
    portal = api('billing/portal', {})
    assert portal['url'].startswith('https://billing.stripe.com/')
    print('PASS: real test invoice webhook activates Cloud, grants exactly 20 credits and opens billing portal', flush=True)
finally:
    if subscription:
        stripe('subscriptions/' + subscription['id'], method='DELETE')
    if customer:
        stripe('customers/' + customer, method='DELETE')
    sql(f"DELETE FROM sessions WHERE user_id='{uid}'")
    print('Test customer/subscription and temporary login session cleaned up; audit account retained', flush=True)
