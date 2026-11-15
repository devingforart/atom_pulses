-- Monthly grants have a distinct ledger kind; the unique invoice reference
-- prevents duplicate grants when Stripe retries a paid-invoice webhook.
ALTER TABLE credit_ledger DROP CONSTRAINT credit_ledger_kind_check;
ALTER TABLE credit_ledger ADD CONSTRAINT credit_ledger_kind_check
  CHECK (kind IN ('purchase','subscription_grant','reserve','consume','release','adjustment'));
