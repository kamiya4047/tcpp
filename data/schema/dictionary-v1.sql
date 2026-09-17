-- Canonical online dictionary schema. PostgreSQL 15+.
-- The IME never queries these tables directly; releases are compiled from approved rows.

CREATE TABLE dictionary_entry (
    entry_id UUID PRIMARY KEY,
    text TEXT NOT NULL CHECK (char_length(text) BETWEEN 1 AND 64),
    locale TEXT NOT NULL DEFAULT 'zh-TW',
    status TEXT NOT NULL CHECK (status IN ('draft','imported','published','retired')),
    revision INTEGER NOT NULL DEFAULT 1,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (text, locale)
);

CREATE TABLE pronunciation (
    pronunciation_id UUID PRIMARY KEY,
    entry_id UUID NOT NULL REFERENCES dictionary_entry(entry_id),
    system TEXT NOT NULL CHECK (system IN ('taiwan-bopomofo-keyboard')),
    input_keys TEXT NOT NULL CHECK (char_length(input_keys) BETWEEN 1 AND 128),
    is_primary BOOLEAN NOT NULL DEFAULT false,
    UNIQUE (entry_id, system, input_keys)
);
CREATE INDEX pronunciation_lookup ON pronunciation (system, input_keys);

CREATE TABLE sense (
    sense_id UUID PRIMARY KEY,
    entry_id UUID NOT NULL REFERENCES dictionary_entry(entry_id),
    part_of_speech TEXT NOT NULL CHECK (part_of_speech IN
        ('noun','verb','adjective','adverb','pronoun','preposition','conjunction','particle','interjection','classifier','idiom','unknown')),
    definition TEXT NOT NULL,
    usage_note TEXT,
    display_order INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE entry_ranking (
    entry_id UUID PRIMARY KEY REFERENCES dictionary_entry(entry_id),
    score DOUBLE PRECISION NOT NULL CHECK (score > 0),
    rarity TEXT NOT NULL CHECK (rarity IN ('common','uncommon','rare','obsolete','unclassified')),
    basis TEXT NOT NULL
);

CREATE TABLE variant_relation (
    canonical_entry_id UUID NOT NULL REFERENCES dictionary_entry(entry_id),
    variant_entry_id UUID NOT NULL REFERENCES dictionary_entry(entry_id),
    relation_kind TEXT NOT NULL CHECK (relation_kind IN ('variant','regional','historical','simplified')),
    PRIMARY KEY (canonical_entry_id, variant_entry_id, relation_kind),
    CHECK (canonical_entry_id <> variant_entry_id)
);

-- Only explicit groups trigger the IME's right-side explanation panel.
CREATE TABLE confusion_group (
    group_id UUID PRIMARY KEY,
    title TEXT NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('draft','published','retired')),
    rationale TEXT NOT NULL
);
CREATE TABLE confusion_group_member (
    group_id UUID NOT NULL REFERENCES confusion_group(group_id),
    entry_id UUID NOT NULL REFERENCES dictionary_entry(entry_id),
    display_order INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (group_id, entry_id)
);
CREATE INDEX confusion_group_by_entry ON confusion_group_member (entry_id, group_id);

CREATE TABLE dictionary_source (
    source_id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    version TEXT NOT NULL,
    url TEXT NOT NULL,
    licence TEXT NOT NULL,
    attribution TEXT NOT NULL,
    redistribution_approved BOOLEAN NOT NULL DEFAULT false
);
CREATE TABLE sense_source (
    sense_id UUID NOT NULL REFERENCES sense(sense_id),
    source_id TEXT NOT NULL REFERENCES dictionary_source(source_id),
    PRIMARY KEY (sense_id, source_id)
);

-- Users propose patches; reviewers publish immutable revisions.
CREATE TABLE edit_proposal (
    proposal_id UUID PRIMARY KEY,
    entry_id UUID NOT NULL REFERENCES dictionary_entry(entry_id),
    base_revision INTEGER NOT NULL,
    patch JSONB NOT NULL,
    rationale TEXT NOT NULL,
    state TEXT NOT NULL CHECK (state IN ('open','accepted','rejected','superseded')),
    proposer_id UUID NOT NULL,
    reviewer_id UUID,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    reviewed_at TIMESTAMPTZ
);
CREATE TABLE entry_revision (
    entry_id UUID NOT NULL REFERENCES dictionary_entry(entry_id),
    revision INTEGER NOT NULL,
    snapshot JSONB NOT NULL,
    published_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    proposal_id UUID REFERENCES edit_proposal(proposal_id),
    PRIMARY KEY (entry_id, revision)
);
