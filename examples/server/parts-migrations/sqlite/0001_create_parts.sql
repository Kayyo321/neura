-- The parts of the example CRUD backend (examples/server/parts-sql.nra).
CREATE TABLE parts (
    id INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    quantity INTEGER NOT NULL CHECK (quantity >= 0)
);
