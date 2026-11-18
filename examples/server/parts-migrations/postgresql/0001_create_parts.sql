-- The parts of the example CRUD backend (examples/server/parts-sql.nra).
CREATE TABLE parts (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    code TEXT NOT NULL CONSTRAINT parts_code_key UNIQUE,
    quantity INTEGER NOT NULL CHECK (quantity >= 0)
);
