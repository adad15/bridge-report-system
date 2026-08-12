drop index if exists ix_defect_photos_observation_number_status;

alter table defect_photos
  drop column if exists match_status;

create index if not exists ix_defect_photos_observation_number
  on defect_photos (defect_observation_id, photo_number);
