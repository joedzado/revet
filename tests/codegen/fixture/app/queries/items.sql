-- @revet:name: all_items
-- @revet:raw
SELECT * FROM items; -- trailing :notparam 'it''s :x'

-- @revet:name: by_owner
-- @revet:model: User
-- @revet:param: owner string
-- @revet:param: lim int
SELECT * FROM items WHERE owner = :owner OR alt = :owner LIMIT :lim;

-- @revet:name: touch
-- @revet:param: id uint64
-- @revet:param: note string?
UPDATE items SET note = :note WHERE id = :id;
