ALTER TABLE clickers ADD COLUMN last_seen INTEGER NOT NULL DEFAULT 0;
CREATE TABLE clicker_invites (
  id TEXT PRIMARY KEY,
  room_id TEXT NOT NULL REFERENCES rooms(id),
  user_id TEXT NOT NULL REFERENCES user(id),
  expires INTEGER NOT NULL,
  redeemed_clicker TEXT REFERENCES clickers(id)
);
CREATE INDEX clicker_invites_expiry ON clicker_invites(expires);
