-- @revet:name: find_user
-- @revet:returns: int64, string?, decimal
-- @revet:param: id int64
SELECT id, nick, amt FROM users WHERE id = :id;

-- @revet:name: count_users
-- @revet:scalar: int64
SELECT count(*) FROM users;

-- @revet:name: upsert
-- @revet:param: name string?
WITH x AS (SELECT 1) INSERT INTO users(name) VALUES (:name);

-- @revet:name: cte_rows
-- @revet:model: User
WITH a AS (SELECT 1) SELECT * FROM a;
