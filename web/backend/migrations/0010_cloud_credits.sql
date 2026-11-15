-- Credits are integer service units, never currency or a Stripe customer balance.
CREATE TABLE credit_wallets (
 user_id UUID PRIMARY KEY REFERENCES users(id),
 available BIGINT NOT NULL DEFAULT 0 CHECK (available >= 0),
 reserved BIGINT NOT NULL DEFAULT 0 CHECK (reserved >= 0),
 blocked BOOLEAN NOT NULL DEFAULT FALSE
);
CREATE TABLE credit_packs (
 id TEXT PRIMARY KEY,
 credits BIGINT NOT NULL CHECK (credits BETWEEN 1 AND 1000000),
 amount_cents BIGINT NOT NULL CHECK (amount_cents BETWEEN 50 AND 1000000),
 currency TEXT NOT NULL DEFAULT 'usd' CHECK (currency ~ '^[a-z]{3}$'),
 active BOOLEAN NOT NULL DEFAULT FALSE
);
INSERT INTO credit_packs(id,credits,amount_cents,currency,active)
VALUES ('starter-15',15,1000,'usd',FALSE);
CREATE TABLE credit_orders (
 id UUID PRIMARY KEY,
 user_id UUID NOT NULL REFERENCES users(id),
 request_key UUID NOT NULL,
 pack_id TEXT NOT NULL REFERENCES credit_packs(id),
 credits BIGINT NOT NULL,
 amount_cents BIGINT NOT NULL,
 currency TEXT NOT NULL,
 session_id TEXT UNIQUE,
 fulfilled BOOLEAN NOT NULL DEFAULT FALSE,
 payment_blocked BOOLEAN NOT NULL DEFAULT FALSE,
 created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
 UNIQUE(user_id, request_key)
);
CREATE TABLE credit_ledger (
 id BIGSERIAL PRIMARY KEY,
 user_id UUID NOT NULL REFERENCES users(id),
 reference TEXT NOT NULL UNIQUE,
 kind TEXT NOT NULL CHECK (kind IN ('purchase','reserve','consume','release','adjustment')),
 available_delta BIGINT NOT NULL,
 reserved_delta BIGINT NOT NULL,
 created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
ALTER TABLE cloud_jobs ADD COLUMN credit_cost BIGINT NOT NULL DEFAULT 0 CHECK (credit_cost >= 0);
ALTER TABLE cloud_jobs ADD COLUMN credit_settled BOOLEAN NOT NULL DEFAULT FALSE;

-- Every terminal path (including cancellation, lease recovery and process crash)
-- settles in the SAME transaction as the job transition. Repeated events are no-ops.
CREATE FUNCTION settle_cloud_credits() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
 IF NEW.credit_cost > 0 AND NOT OLD.credit_settled AND NEW.status IN ('completed','failed','cancelled') THEN
   UPDATE credit_wallets SET reserved=reserved-NEW.credit_cost,
     available=available+CASE WHEN NEW.status='completed' THEN 0 ELSE NEW.credit_cost END
     WHERE user_id=NEW.user_id;
   INSERT INTO credit_ledger(user_id,reference,kind,available_delta,reserved_delta)
     VALUES(NEW.user_id,'settle:'||NEW.id::text,
       CASE WHEN NEW.status='completed' THEN 'consume' ELSE 'release' END,
       CASE WHEN NEW.status='completed' THEN 0 ELSE NEW.credit_cost END,-NEW.credit_cost);
   NEW.credit_settled=TRUE;
 END IF;
 RETURN NEW;
END $$;
CREATE TRIGGER cloud_credit_settlement BEFORE UPDATE OF status ON cloud_jobs
 FOR EACH ROW EXECUTE FUNCTION settle_cloud_credits();
CREATE INDEX credit_ledger_user_history ON credit_ledger(user_id,id DESC);
