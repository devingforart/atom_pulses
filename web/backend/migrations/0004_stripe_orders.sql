CREATE TABLE stripe_orders (
    checkout_session_id TEXT PRIMARY KEY,
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    stripe_customer_id TEXT NOT NULL,
    payment_intent_id TEXT,
    charge_id TEXT,
    invoice_id TEXT,
    price_id TEXT NOT NULL,
    product TEXT NOT NULL,
    mode TEXT NOT NULL,
    payment_status TEXT NOT NULL,
    fulfillment_status TEXT NOT NULL,
    amount_total BIGINT,
    currency TEXT,
    tax_total BIGINT,
    livemode BOOLEAN NOT NULL,
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL
);

CREATE UNIQUE INDEX stripe_orders_payment_intent_idx
    ON stripe_orders(payment_intent_id) WHERE payment_intent_id IS NOT NULL;
CREATE INDEX stripe_orders_charge_idx
    ON stripe_orders(charge_id) WHERE charge_id IS NOT NULL;
CREATE INDEX stripe_orders_user_idx ON stripe_orders(user_id, updated_at DESC);
