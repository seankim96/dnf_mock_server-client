-- MySQL 8.x schema used by the optional MySqlPlayerRepository backend.
-- Database and user creation are intentionally left to deployment tooling.

CREATE TABLE IF NOT EXISTS players (
    player_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    name VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    level INT UNSIGNED NOT NULL DEFAULT 1,
    skill_points INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (player_id),
    UNIQUE KEY players_name_unique (name),
    CONSTRAINT players_name_length_check
        CHECK (CHAR_LENGTH(name) BETWEEN 1 AND 16),
    CONSTRAINT players_level_check CHECK (level >= 1)
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS player_skills (
    player_id BIGINT UNSIGNED NOT NULL,
    skill_id INT UNSIGNED NOT NULL,
    skill_level INT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (player_id, skill_id),
    CONSTRAINT player_skills_skill_id_check CHECK (skill_id > 0),
    CONSTRAINT player_skills_level_check CHECK (skill_level >= 1),
    CONSTRAINT player_skills_player_foreign_key
        FOREIGN KEY (player_id) REFERENCES players(player_id)
        ON DELETE CASCADE
) ENGINE = InnoDB;
