"""Read existing activity through public Growth; temporary admin session only."""
import hashlib,json,secrets,sqlite3,time,urllib.request,urllib.error

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
