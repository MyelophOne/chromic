#!/usr/bin/env bash
set -euo pipefail

output_file=${1:?GitHub output file is required}
notes_file=${2:?Release notes file is required}

last_tag=""
while IFS= read -r tag; do
  if [[ "$tag" =~ ^v?[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    last_tag=$tag
    break
  fi
done < <(git tag --merged HEAD --sort=-version:refname)

if [[ -n "$last_tag" ]]; then
  range="$last_tag..HEAD"
  base_version=${last_tag#v}
  first_release=false
else
  range="HEAD"
  base_version="0.0.0"
  first_release=true
fi

level=0
breaking_header_re='^[[:alnum:]-]+(\([^)]*\))?!:'
type_header_re='^([[:alnum:]-]+)(\([^)]*\))?:'
declare -a breaking_notes=()
declare -a feature_notes=()
declare -a fix_notes=()
declare -a maintenance_notes=()

has_breaking_footer() {
  local body_line
  while IFS= read -r body_line; do
    if [[ "$body_line" =~ ^BREAKING[[:space:]]CHANGES?: ]]; then
      return 0
    fi
  done <<< "$1"
  return 1
}

while IFS= read -r -d '' record; do
  hash=${record%%$'\x1f'*}
  remainder=${record#*$'\x1f'}
  subject=${remainder%%$'\x1f'*}
  body=${remainder#*$'\x1f'}
  short_hash=${hash:0:7}
  line="* $short_hash $subject"

  if [[ "$subject" =~ $breaking_header_re ]] || has_breaking_footer "$body"; then
    level=3
    breaking_notes+=("$line")
    continue
  fi

  type=""
  if [[ "$subject" =~ $type_header_re ]]; then
    type=${BASH_REMATCH[1],,}
  fi

  case "$type" in
    feat)
      if (( level < 2 )); then level=2; fi
      feature_notes+=("$line")
      ;;
    fix|perf)
      if (( level < 1 )); then level=1; fi
      fix_notes+=("$line")
      ;;
    refactor|test|build|ci|style|docs|revert|chore|wip)
      maintenance_notes+=("$line")
      ;;
  esac
done < <(git log --reverse --format='%H%x1f%s%x1f%b%x00' "$range")

if [[ "$first_release" == false ]] && (( level == 0 )); then
  echo "should_release=false" >> "$output_file"
  exit 0
fi

if [[ "$first_release" == true ]]; then
  version="0.1.0"
else
  IFS=. read -r major minor patch <<< "$base_version"
  case "$level" in
    3) ((major += 1)); minor=0; patch=0 ;;
    2) ((minor += 1)); patch=0 ;;
    1) ((patch += 1)) ;;
  esac
  version="$major.$minor.$patch"
fi

{
  echo "should_release=true"
  echo "version=$version"
  echo "last_tag=$last_tag"
} >> "$output_file"

write_section() {
  local title=$1
  shift
  if (( $# == 0 )); then return; fi
  echo "## $title" >> "$notes_file"
  printf '%s\n' "$@" >> "$notes_file"
  echo >> "$notes_file"
}

: > "$notes_file"
write_section "Breaking changes" "${breaking_notes[@]}"
write_section "Features" "${feature_notes[@]}"
write_section "Fixes and performance" "${fix_notes[@]}"
write_section "Maintenance" "${maintenance_notes[@]}"

if [[ "$first_release" == true ]] &&
   (( ${#breaking_notes[@]} + ${#feature_notes[@]} + ${#fix_notes[@]} + ${#maintenance_notes[@]} == 0 )); then
  printf '## Initial release\n\nFirst published build of Chromic.\n\n' >> "$notes_file"
fi

repository_url="${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-owner/repository}"
if [[ -n "$last_tag" ]]; then
  echo "**Full changelog:** $repository_url/compare/$last_tag...v$version" >> "$notes_file"
else
  echo "**Full changelog:** $repository_url/commits/v$version" >> "$notes_file"
fi
