CREATE TABLE promotion_redemptions (
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    campaign TEXT NOT NULL,
    redeemed_at BIGINT NOT NULL,
    PRIMARY KEY (user_id, campaign)
);

CREATE INDEX promotion_redemptions_campaign_idx ON promotion_redemptions(campaign);
