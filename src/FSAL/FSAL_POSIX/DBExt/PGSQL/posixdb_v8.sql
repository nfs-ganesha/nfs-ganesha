
/*
 * BIGINT are signed integers on 8 bytes !
 */
CREATE TABLE Handle (
  handleId  BIGSERIAL, 
  handleTs  INT,
  deviceId  BIGINT NOT NULL, 
  inode     BIGINT NOT NULL, 
  ctime     INT, 
  nlink     SMALLINT DEFAULT 1,
  ftype     SMALLINT,
  PRIMARY KEY(handleId, handleTs),
  UNIQUE (deviceId, inode)
);

CREATE TABLE Parent (
  handleId        BIGINT NOT NULL,
  handleTs        INT NOT NULL,
  handleIdParent  BIGINT,
  handleTsParent  INT,
  name            TEXT,
  UNIQUE (handleIdParent, handleTsParent, name),
  FOREIGN KEY (handleId, handleTs) REFERENCES Handle(handleId, handleTs) ON DELETE CASCADE,
  FOREIGN KEY (handleidparent, handletsparent) REFERENCES handle(handleid, handlets) ON DELETE CASCADE
);
CREATE INDEX parent_handle_index ON Parent (handleId, HandleTs);

/*
 * Fonctions
 */
CREATE OR REPLACE FUNCTION buildOnePath( handleid Parent.handleid%TYPE, handlets Parent.handlets%TYPE ) RETURNS text AS $$
DECLARE
    posix_path text := '';
    parent_handleid bigint;
    parent_handlets int;
    parent_entry Parent%ROWTYPE;
BEGIN
    /* build one path */
    parent_handleid := handleid;
    parent_handlets := handlets;
    SELECT INTO parent_entry * FROM Parent WHERE Parent.handleid=parent_handleid AND Parent.handlets=parent_handlets;
    WHILE parent_handleid != parent_entry.handleidparent OR parent_handlets != parent_entry.handletsparent LOOP
      parent_handleid := parent_entry.handleidparent;
      parent_handlets := parent_entry.handletsparent;
      posix_path := '/' || parent_entry.name || posix_path;
      SELECT INTO parent_entry * FROM Parent WHERE Parent.handleid=parent_handleid AND Parent.handlets=parent_handlets;
    END LOOP;

  RETURN posix_path;
END;
$$ LANGUAGE plpgsql;
