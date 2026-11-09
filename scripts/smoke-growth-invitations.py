"""Production-host invitation smoke test. No music, mail or payments."""
import hashlib, json, secrets, sqlite3, subprocess, time, urllib.request, uuid

growth='https://growth.pulso-audio.com'
cloud='https://pulso-audio.com'
admin_token=secrets.token_hex(32)
user_token=secrets.token_hex(32)
uid=str(uuid.uuid4())
now=int(time.time())
invitation=None
def sql(query):
    return subprocess.check_output(['docker','exec','pulso-postgres-1','psql','-U','pulso','-d','pulso','-At','-v','ON_ERROR_STOP=1','-c',query],text=True).strip()
def call(origin,path,body=None,cookie=None):
    headers={'Content-Type':'application/json','Origin':origin,'X-Growth-Request':'1'}
    if cookie: headers['Cookie']=cookie
    request=urllib.request.Request(origin+path,data=json.dumps(body).encode() if body is not None else None,headers=headers)
    with urllib.request.urlopen(request,timeout=30) as response:
        return json.load(response)
c=sqlite3.connect('/www/Pulso-growth-data/growth.sqlite3')
try:
    c.execute('INSERT INTO sessions(hash,expires) VALUES(?,?)',(hashlib.sha256(admin_token.encode()).hexdigest(),now+300));c.commit()
    sql(f"INSERT INTO users(id,email,display_name,password_hash,email_verified,created_at) VALUES('{uid}','invitation-{uid}@pulso.invalid','Internal invitation deployment check','!disabled',TRUE,{now}); INSERT INTO sessions(token_hash,user_id,expires_at,created_at) VALUES('{hashlib.sha256(user_token.encode()).hexdigest()}','{uid}',{now+300},{now});")
    admin_cookie='growth_session='+admin_token
    user_cookie='pulso_session='+user_token
    settings=call(growth,'/api/admin/invitations/connection',cookie=admin_cookie)
    assert settings['configured'] and settings['url']==cloud
    invitation={'id':str(uuid.uuid4()),'code':'TEST-'+secrets.token_hex(16).upper(),'label':'Deployment verification (revoked after test)',
        'targetEmail':f'invitation-{uid}@pulso.invalid','credits':6,'dailyLimit':1,'weeklyLimit':2,'monthlyLimit':2,'maxRedemptions':1,'expiresAt':now+600}
    created=call(growth,'/api/admin/invitations',invitation,admin_cookie)
    assert created['id']==invitation['id']
    redeemed=call(cloud,'/api/billing/redeem-promo',{'code':invitation['code']},user_cookie)
    assert redeemed['granted'] and redeemed['cloudOnly']
    state=call(cloud,'/api/cloud/status',cookie=user_cookie)
    assert state['available'] and not state['creditsEnabled']
    assert state['demo']['creditsRemaining']==6
    assert [w['limit'] for w in state['demo']['windows']]==[1,2,2]
    call(growth,'/api/admin/invitations/'+invitation['id']+'/revoke',{},admin_cookie)
    assert not call(cloud,'/api/cloud/status',cookie=user_cookie)['available']
    print('PASS: public Growth admin → private bridge → targeted Cloud redemption → 6 credits and 1/2/2 limits → revocation',flush=True)
finally:
    if invitation:
        try: call(growth,'/api/admin/invitations/'+invitation['id']+'/revoke',{},'growth_session='+admin_token)
        except Exception: pass
    c.execute('DELETE FROM sessions WHERE hash=?',(hashlib.sha256(admin_token.encode()).hexdigest(),));c.commit();c.close()
    sql(f"DELETE FROM users WHERE id='{uid}' AND email='invitation-{uid}@pulso.invalid'")
    print('Temporary account and admin session removed; verification invitation remains revoked in audit',flush=True)
