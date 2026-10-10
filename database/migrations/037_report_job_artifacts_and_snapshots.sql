-- Temporary artifacts outlive job/history rows so failed deletion remains retryable.
alter table report_generation_jobs add column if not exists template_snapshot_json jsonb;
create table if not exists report_job_artifacts (
    job_id uuid primary key,
    expected_output_path text,
    cleanup_state text not null default 'active'
        check (cleanup_state in ('active','pending','cleaning','blocked','cleaned')),
    attempts integer not null default 0 check (attempts >= 0),
    next_attempt_at timestamptz not null default now(),
    claimed_at timestamptz,
    claim_token uuid,
    last_error text,
    cleaned_at timestamptz,
    created_at timestamptz not null default now()
);
create index if not exists ix_report_job_artifacts_cleanup
    on report_job_artifacts(cleanup_state, next_attempt_at);
insert into report_job_artifacts(job_id, expected_output_path, cleanup_state)
select id, temporary_file_path,
       case when status='failed' then 'blocked'
            when status='expired' then 'pending' else 'active' end
from report_generation_jobs on conflict(job_id) do nothing;
comment on table report_job_artifacts is
'Temporary report directory ownership and retry journal; deliberately no cascading job foreign key.';

-- Keep a monotonic slot revision even after deletion (prevents absent -> manual -> absent ABA).
alter table bridges add column if not exists location_map_revision bigint not null default 0;
create or replace function bump_location_map_revision() returns trigger language plpgsql as $$
begin
    if TG_OP = 'DELETE' then
        if OLD.slot = 'LOCATION_MAP' then
            update bridges set location_map_revision=location_map_revision+1 where id=OLD.bridge_id;
        end if;
        return OLD;
    end if;
    if NEW.slot = 'LOCATION_MAP' then
        update bridges set location_map_revision=location_map_revision+1 where id=NEW.bridge_id;
    end if;
    return NEW;
end $$;
drop trigger if exists trg_bridge_media_location_revision on bridge_media;
create trigger trg_bridge_media_location_revision after insert or update or delete on bridge_media
for each row execute function bump_location_map_revision();
