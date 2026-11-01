CREATE TABLE clickers (
  id TEXT PRIMARY KEY,
  token_hash TEXT NOT NULL,
  name TEXT NOT NULL,
  user_id TEXT REFERENCES user(id),
  pairing_hash TEXT UNIQUE,
  pairing_expires INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  revoked INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX clickers_owner ON clickers(user_id);
CREATE TABLE clicker_routes (
  clicker_id TEXT NOT NULL REFERENCES clickers(id) ON DELETE CASCADE,
  room_id TEXT NOT NULL REFERENCES rooms(id),
  PRIMARY KEY (clicker_id, room_id)
);
