from datetime import datetime
from os import makedirs
from pathlib import Path
from sqlite3 import Connection, connect
from typing import Iterable, List, Optional, Tuple

schema = '''

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS breakpoint (
    id INTEGER PRIMARY KEY,
    file TEXT NOT NULL,
    line INTEGER NOT NULL,
    condition TEXT,
    datetime TIMESTAMP NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE IF NOT EXISTS expression (
    id INTEGER PRIMARY KEY,
    expression TEXT NOT NULL,
    breakpoint_id INTEGER NOT NULL,
    FOREIGN KEY (breakpoint_id) REFERENCES breakpoint(id)
        ON DELETE CASCADE
        ON UPDATE CASCADE
);

CREATE TABLE IF NOT EXISTS hit (
    id INTEGER PRIMARY KEY,
    breakpoint_id INTEGER,
    datetime TIMESTAMP NOT NULL DEFAULT (unixepoch()),
    FOREIGN KEY (breakpoint_id) REFERENCES breakpoint(id)
        ON DELETE CASCADE
        ON UPDATE CASCADE
);

CREATE TABLE IF NOT EXISTS expression_value (
    id INTEGER PRIMARY KEY,
    hit_id INTEGER NOT NULL,
    expression_id INTEGER,
    value TEXT NOT NULL,
    FOREIGN KEY (hit_id) REFERENCES hit(id)
        ON DELETE CASCADE
        ON UPDATE CASCADE
    FOREIGN KEY (expression_id) REFERENCES expression(id)
        ON DELETE CASCADE
        ON UPDATE CASCADE
);

'''

Breakpoint = Tuple[int, str, int, Optional[str], Iterable[str]]
Hit = Tuple[int, datetime, Iterable[str]]

class CloudebugDB:
    def __init__(self, base: Path, init: bool = False):
        makedirs(base / '.cloudebug', exist_ok=True)
        self.db: Connection = connect(base / '.cloudebug' / 'db.sqlite')
        if init:
            self.db.executescript(schema)

    def get_breakpoint(self, id: int) -> Optional[Breakpoint]:
        breakpoint = self.db.execute('SELECT file, line, condition FROM breakpoint WHERE id = ?', (id,)).fetchone()
        if breakpoint is None:
            return None
        file, line, condition = breakpoint
        expressions = self.get_expressions(id)
        return int(id), str(file), int(line), condition, expressions

    def get_breakpoints(self) -> Iterable[Breakpoint]:
        breakpoints: List[Breakpoint] = []
        for breakpoint in self.db.execute('SELECT id, file, line, condition FROM breakpoint'):
            id, file, line, condition = breakpoint
            expressions = self.get_expressions(id)
            breakpoints.append((int(id), str(file), int(line), condition, expressions))
        return breakpoints

    def get_hits(self, breakpoint_id: int) -> Iterable[Hit]:
        hits: List[Hit] = []
        for hit in self.db.execute('SELECT id, datetime FROM hit WHERE breakpoint_id = ?', (breakpoint_id,)):
            id, hit_datetime = hit
            values = self.get_values(id)
            hits.append((int(id), datetime.utcfromtimestamp(hit_datetime), values))
        return hits

    def get_expressions(self, breakpoint_id: int) -> List[str]:
        return [str(row[0]) for row in self.db.execute('SELECT expression FROM expression WHERE breakpoint_id = ?', (breakpoint_id,))]

    def get_values(self, hit_id: int) -> List[str]:
        return [str(row[0]) for row in self.db.execute('SELECT value FROM expression_value WHERE hit_id = ?', (hit_id,))]

    def add_breakpoint(self, file: str, line: int, condition: Optional[str], expressions: Iterable[str]) -> int:
        try:
            cursor = self.db.execute('INSERT INTO breakpoint (file, line, condition) VALUES (?, ?, ?)', (file, line, condition))
            breakpoint_id = cursor.lastrowid
            if breakpoint_id is None:
                raise Exception('No rowid returned after breakpoint insertion.')
            for expression in expressions:
                self.db.execute('INSERT INTO expression (expression, breakpoint_id) VALUES (?, ?)', (expression, breakpoint_id))
            self.db.commit()
            return breakpoint_id
        finally:
            if self.db.in_transaction:
                self.db.rollback()

    def remove_breakpoint(self, id: int):
        self.db.execute('DELETE FROM breakpoint WHERE id = ?', (id,))
        self.db.commit()

    def log_hits(self, breakpoint_ids: Iterable[int], values: List[List[str]]) -> List[int]:
        try:
            get_expression_ids_query = f'''SELECT id, breakpoint_id FROM expression WHERE breakpoint_id IN ({
                ",".join([str(id) for id in breakpoint_ids])
            })'''
            expression_ids = [(int(row[0]), int(row[1])) for row in self.db.execute(get_expression_ids_query)]
            hit_ids = []
            values_db: List[Tuple[int, int, str]] = []
            for idx, breakpoint_id in enumerate(breakpoint_ids):
                cursor = self.db.execute('INSERT INTO hit (breakpoint_id) VALUES (?)', (breakpoint_id,))
                hit_id = cursor.lastrowid
                if hit_id is None:
                    raise Exception('No rowid returned after hit insertion.')
                hit_ids.append(hit_id)
                expression_ids_filtered = [id for id, bp_id in expression_ids if breakpoint_id == bp_id]
                values_db += [(hit_id, expression_ids_filtered[value_idx], value) for value_idx, value in enumerate(values[idx])]
            self.db.executemany('INSERT INTO expression_value (hit_id, expression_id, value) VALUES (?, ?, ?)', values_db)
            self.db.commit()
            return hit_ids
        finally:
            if self.db.in_transaction:
                self.db.rollback()
