-- @revet:name: create_schema
CREATE TABLE IF NOT EXISTS users (
    id    INTEGER PRIMARY KEY AUTOINCREMENT,
    name  TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE
);

-- @revet:name: seed
INSERT OR IGNORE INTO users (name, email) VALUES
    ('Alice',   'alice@example.com'),
    ('Bob',     'bob@example.com'),
    ('Charlie', 'charlie@example.com');

-- @revet:name: count
-- @revet:raw
SELECT COUNT(*) FROM users;

-- @revet:name: all
-- @revet:model: User
SELECT id, name, email FROM users ORDER BY id;

-- @revet:name: find_by_id
-- @revet:model: User
-- @revet:param: id int
SELECT id, name, email FROM users WHERE id = :id LIMIT 1;

-- @revet:name: search
-- @revet:model: User
-- @revet:param: q string
SELECT id, name, email FROM users
WHERE name LIKE '%' || :q || '%' OR email LIKE '%' || :q || '%'
ORDER BY id;

-- @revet:name: create
-- @revet:param: name string
-- @revet:param: email string
INSERT INTO users (name, email) VALUES (:name, :email);

-- @revet:name: delete_by_id
-- @revet:param: id int
DELETE FROM users WHERE id = :id;
