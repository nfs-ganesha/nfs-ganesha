
CREATE TABLE IF NOT EXISTS Handle (
  handleid  BIGINT UNSIGNED AUTO_INCREMENT,
  handlets  INT,
  deviceid  BIGINT NOT NULL, 
  inode     BIGINT NOT NULL, 
  ctime     INT, 
  nlink     SMALLINT DEFAULT 1,
  ftype     SMALLINT,
  PRIMARY KEY(handleid, handlets),
  UNIQUE (deviceid, inode)
) TYPE = InnoDB;

CREATE TABLE IF NOT EXISTS Parent (
  handleid        BIGINT UNSIGNED NOT NULL,
  handlets        INT,
  handleidparent  BIGINT UNSIGNED,
  handletsparent  INT,
  name            VARCHAR(255),
  UNIQUE (handleidparent, handletsparent, name),
  FOREIGN KEY (handleid, handlets) REFERENCES Handle(handleid, handlets) ON DELETE CASCADE,
  FOREIGN KEY (handleidparent, handletsparent) REFERENCES Handle(handleid, handlets) ON DELETE CASCADE
) TYPE = InnoDB;
CREATE INDEX parent_handle_index ON Parent (handleid, handlets);

