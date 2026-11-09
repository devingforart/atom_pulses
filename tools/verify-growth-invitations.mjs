// Local integration test: real Growth HTTP -> real Cloud API -> isolated lab DB.
// No OpenAI, Stripe, email or social publishing calls.
import assert from 'node:assert/strict';
import {spawn,execFileSync} from 'node:child_process';
import {mkdtempSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {resolve,join} from 'node:path';
import {randomUUID,randomBytes} from 'node:crypto';
const core=process.cwd(),growth=resolve('../pulso_growth'),cloud='http://localhost:4323',panel='http://localhost:8791';
const token=randomBytes(32).toString('hex'),password='Local-invitation-test-only-2026!',suffix=Date.now();
const pg=resolve('build/demo-lab/postgres/pgsql/bin/psql.exe');
const q=s=>`'${String(s).replaceAll("'","''")}'`;
const sql=query=>execFileSync(pg,['-h','127.0.0.1','-p','55432','-U','pulso','-d','pulso_demo_lab','-X','-qAt','-v','ON_ERROR_STOP=1'],{input:query,encoding:'utf8',windowsHide:true}).trim();
async function call(base,path,body,cookie,extra={}){
  const r=await fetch(base+path,{method:body===undefined?'GET':'POST',headers:{'Content-Type':'application/json',Origin:base,'X-Growth-Request':'1',...(cookie?{Cookie:cookie}:{}),...extra},body:body===undefined?undefined:JSON.stringify(body)});
  return {status:r.status,data:await r.json().catch(()=>null),cookie:r.headers.get('set-cookie')?.split(';')[0]};
}
async function ready(base){for(let i=0;i<60;i++){try{const r=await fetch(base+'/api/health');if(r.ok)return}catch{}await new Promise(r=>setTimeout(r,500))}throw Error('Server did not start: '+base)}
for(const base of [cloud,panel]){let occupied=false;try{occupied=(await fetch(base+'/api/health')).ok}catch{}assert.equal(occupied,false,'Refusing an occupied test port')}
const processes=[];
try{
 const hash=execFileSync(join(growth,'backend/target/debug/pulso-growth.exe'),['hash-password'],{input:password,encoding:'utf8',windowsHide:true}).trim();
 processes.push(spawn('powershell.exe',['-NoProfile','-File',resolve('scripts/start-demo-lab.ps1'),'-Port','4323'],{cwd:core,env:{...process.env,PULSO_GROWTH_ADMIN_TOKEN:token},windowsHide:true,stdio:'ignore'}));
 await ready(cloud);assert.equal(sql('SELECT current_database()'),'pulso_demo_lab');
 processes.push(spawn(join(growth,'backend/target/debug/pulso-growth.exe'),[],{cwd:growth,env:{...process.env,DATA_DIR:mkdtempSync(join(tmpdir(),'pulso-invitations-')),BIND:'127.0.0.1:8791',PUBLIC_URL:panel,ADMIN_PASSWORD_HASH:hash,MASTER_KEY:randomBytes(32).toString('hex'),GROWTH_CLOUD_API_URL:cloud,GROWTH_CLOUD_ADMIN_TOKEN:token,ENABLE_LIVE_PUBLISH:'false',DAILY_SOCIAL_ENABLED:'false',MAIL_MODE:'local',FRONTEND_DIR:join(growth,'frontend/dist')},windowsHide:true,stdio:'ignore'}));
 await ready(panel);
 assert.equal((await call(panel,'/api/admin/invitations')).status,401);
 assert.equal((await call(cloud,'/api/growth/invitations')).status,401);
 const login=await call(panel,'/api/login',{password});assert.equal(login.status,200);const admin=login.cookie;
 const offers=[];
 async function offer(overrides={}){
  const input={id:randomUUID(),code:'TEST-'+randomUUID().replaceAll('-',''),label:'Local integration '+suffix,credits:36,dailyLimit:2,weeklyLimit:4,monthlyLimit:6,maxRedemptions:1,targetEmail:null,expiresAt:null,...overrides};
  const result=await call(panel,'/api/admin/invitations',input,admin);assert.equal(result.status,200,JSON.stringify(result.data));offers.push(input);return input;
 }
 async function account(label){const email=`invite-${label}-${suffix}@pulso.invalid`;const r=await call(cloud,'/api/auth/register',{email,password,displayName:'Invitation lab'});assert.ok(r.status<300,JSON.stringify(r.data));sql(`UPDATE users SET email_verified=TRUE WHERE id=${q(r.data.user.id)}`);return{id:r.data.user.id,email,cookie:r.cookie}}
 const user=await account('target'),other=await account('other');
 const offer1=await offer({targetEmail:user.email,credits:6});
 assert.equal((await call(cloud,'/api/billing/redeem-promo',{code:offer1.code},other.cookie)).status,400);
 const unverified=await call(cloud,'/api/auth/register',{email:`unverified-${suffix}@pulso.invalid`,password,displayName:'Unverified'});
 assert.equal((await call(cloud,'/api/billing/redeem-promo',{code:offer1.code},unverified.cookie)).status,403);
 let redeem=await call(cloud,'/api/billing/redeem-promo',{code:offer1.code.toLowerCase()},user.cookie);assert.equal(redeem.status,200);
 assert.equal((await call(cloud,'/api/billing/redeem-promo',{code:offer1.code},user.cookie)).data.alreadyRedeemed,true);
 let status=await call(cloud,'/api/cloud/status',undefined,user.cookie);
 assert.equal(status.data.available,true);assert.equal(status.data.creditsEnabled,false);
 assert.deepEqual(status.data.demo.windows.map(w=>w.limit),[2,4,6]);assert.equal(status.data.demo.creditsRemaining,6);
 const me=await call(cloud,'/api/auth/me',undefined,user.cookie);assert.equal(me.data.user?.studioOwned??me.data.studioOwned,false,'Cloud promo must not grant Studio');
 const job=()=>({prompt:'Invitation test only',durationSeconds:60,bpm:120,idempotencyKey:randomUUID()});
 const simultaneous=await Promise.all([call(cloud,'/api/cloud/jobs',job(),user.cookie),call(cloud,'/api/cloud/jobs',job(),user.cookie)]);
 assert.deepEqual(simultaneous.map(r=>r.status).sort(),[201,429]);
 assert.equal(sql(`SELECT invitation_id FROM cloud_jobs WHERE id=${q(simultaneous.find(r=>r.status===201).data.id)}`),offer1.id,'Usage must belong to its invitation, not paid credit ledger');
 sql(`UPDATE cloud_jobs SET status='failed' WHERE user_id=${q(user.id)}`);
 assert.equal((await call(cloud,'/api/cloud/status',undefined,user.cookie)).data.demo.creditsRemaining,6);
 for(let i=0;i<2;i++){const r=await call(cloud,'/api/cloud/jobs',job(),user.cookie);assert.equal(r.status,201,JSON.stringify(r.data));sql(`UPDATE cloud_jobs SET status='completed',updated_at=extract(epoch from now())::bigint WHERE id=${q(r.data.id)}`)}
 status=await call(cloud,'/api/cloud/status',undefined,user.cookie);assert.equal(status.data.demo.creditsRemaining,0);assert.equal(status.data.demo.canCompose,false);
 assert.equal((await call(cloud,'/api/cloud/jobs',job(),user.cookie)).status,429);
 sql(`UPDATE cloud_jobs SET updated_at=extract(epoch from now())::bigint-31*86400 WHERE user_id=${q(user.id)} AND status='completed'`);
 assert.equal((await call(cloud,'/api/cloud/status',undefined,user.cookie)).data.demo.windows[0].remaining,2);
 assert.equal((await call(cloud,'/api/cloud/jobs',job(),user.cookie)).status,429,'Total credits must not renew when rolling limits expire');
 const previousCreated=sql(`SELECT redeemed_at FROM cloud_invitation_redemptions WHERE user_id=${q(user.id)}`);
 await call(cloud,'/api/billing/redeem-promo',{code:offer1.code},user.cookie);
 assert.equal(sql(`SELECT redeemed_at FROM cloud_invitation_redemptions WHERE user_id=${q(user.id)}`),previousCreated);
 assert.equal((await call(panel,'/api/admin/invitations/'+offer1.id+'/revoke',{},admin)).status,200);
 assert.equal((await call(cloud,'/api/cloud/status',undefined,user.cookie)).data.available,false);
 const shared=await offer({maxRedemptions:1});
 const candidates=await Promise.all([account('race1'),account('race2')]);
 const races=await Promise.all(candidates.map(u=>call(cloud,'/api/billing/redeem-promo',{code:shared.code},u.cookie)));
 assert.deepEqual(races.map(r=>r.status).sort(),[200,400]);
 for(const [window,age,limit] of [['daily',100,2],['weekly',2*86400,4],['monthly',8*86400,6]]){
  const u=await account(window),o=await offer();await call(cloud,'/api/billing/redeem-promo',{code:o.code},u.cookie);
  for(let i=0;i<limit;i++)sql(`INSERT INTO cloud_jobs(id,user_id,idempotency_key,prompt,duration_seconds,bpm,behavior,seed,status,stage,created_at,updated_at) VALUES(${q(randomUUID())},${q(u.id)},${q(randomUUID())},'LOCAL',60,120,'adaptive',1,'completed','ready',extract(epoch from now())::bigint-${age},extract(epoch from now())::bigint-${age})`);
  assert.equal((await call(cloud,'/api/cloud/jobs',job(),u.cookie)).status,429,window+' quota');
 }
 const expiredUser=await account('expired'),expired=await offer();sql(`UPDATE cloud_invitations SET expires_at=1 WHERE id=${q(expired.id)}`);
 assert.equal((await call(cloud,'/api/billing/redeem-promo',{code:expired.code},expiredUser.cookie)).status,400);
 const list=await call(panel,'/api/admin/invitations',undefined,admin);assert.ok(list.data.items.some(i=>i.id===offer1.id&&i.redemptions===1));assert.ok(!JSON.stringify(list.data).includes(offer1.code));
 const paid=await account('paid'),paidOffer=await offer();await call(cloud,'/api/billing/redeem-promo',{code:paidOffer.code},paid.cookie);
 sql(`INSERT INTO subscriptions(user_id,status,updated_at) VALUES(${q(paid.id)},'active',extract(epoch from now())::bigint)`);
 assert.equal((await call(cloud,'/api/cloud/status',undefined,paid.cookie)).data.demo,null,'Paid subscription must not be downgraded');
 const studio=await account('studio'),studioOffer=await offer();await call(cloud,'/api/billing/redeem-promo',{code:studioOffer.code},studio.cookie);
 sql(`INSERT INTO entitlements(user_id,product,status,source,created_at,updated_at) VALUES(${q(studio.id)},'studio','active','stripe',extract(epoch from now())::bigint,extract(epoch from now())::bigint)`);
 assert.equal((await call(cloud,'/api/cloud/status',undefined,studio.cookie)).data.demo,null,'Paid Studio must not be downgraded');
 console.log('PASS Growth → Cloud: admin security, verified targeted email, hashed codes, idempotent redemption, concurrent admission and last slot, 3-credit budget, failed-work refund, rolling day/week/month caps, expiry, revocation, no Studio license.');
}finally{for(const p of processes.reverse()){if(p.pid)try{execFileSync('taskkill',['/PID',String(p.pid),'/T','/F'],{windowsHide:true,stdio:'ignore'})}catch{}}}
