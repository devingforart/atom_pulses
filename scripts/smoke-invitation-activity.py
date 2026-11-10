"""Read existing activity through public Growth; temporary admin session only."""
import hashlib,json,secrets,sqlite3,subprocess,time,urllib.request,urllib.error

origin='https://growth.pulso-audio.com'
token=secrets.token_hex(32)
digest=hashlib.sha256(token.encode()).hexdigest()
c=sqlite3.connect('/www/Pulso-growth-data/growth.sqlite3')
def get(path,authenticated=True):
    headers={'Cookie':'growth_session='+token} if authenticated else {}
    with urllib.request.urlopen(urllib.request.Request(origin+path,headers=headers),timeout=30) as r:
        return json.load(r)
try:
    c.execute('INSERT INTO sessions(hash,expires) VALUES(?,?)',(digest,int(time.time())+300));c.commit()
    offers=get('/api/admin/invitations')['items']
    assert offers,'Expected at least one existing invitation'
    assert 'emailStatus' in offers[0], 'Cloud binary does not include invitation email support'
    installed=subprocess.check_output(['docker','exec','pulso-postgres-1','psql','-U','pulso','-d','pulso','-Atc',"SELECT EXISTS(SELECT 1 FROM _sqlx_migrations WHERE version=17 AND success) AND to_regclass('public.cloud_invitation_emails') IS NOT NULL"],text=True).strip()
    assert installed=='t', 'Invitation email migration missing'
    request=urllib.request.Request(origin+'/api/admin/invitations/'+offers[0]['id']+'/send',data=b'{"code":"INVALID-PROBE"}',headers={'Cookie':'growth_session='+token,'Content-Type':'application/json','Origin':origin,'X-Growth-Request':'1'})
    try:
        urllib.request.urlopen(request,timeout=30)
        raise AssertionError('Invalid invitation code accepted for email')
    except urllib.error.HTTPError as error:
        assert error.code==400, 'Email route must reject an invalid code without sending'
    path='/api/admin/invitations/'+offers[0]['id']+'/activity'
    report=get(path)
    assert report['scope']=='invitation_attributed_jobs' and report['pageSize']==25
    assert isinstance(report['users'],list) and isinstance(report['summary']['completed'],int)
    if report['users']:
        filtered=get(path+'?userId='+report['users'][0]['id'])
        assert all(j['userId']==report['users'][0]['id'] for j in filtered['jobs'])
    try:
        get(path,False)
        raise AssertionError('Anonymous activity exposed')
    except urllib.error.HTTPError as error:
        assert error.code==401
    with urllib.request.urlopen(origin+'/admin',timeout=30) as r:
        assert r.status==200
    print('PASS public Growth activity: authorized summary, user attribution, anonymous denied; no compositions or invitations created')
finally:
    c.execute('DELETE FROM sessions WHERE hash=?',(digest,));c.commit();c.close()
