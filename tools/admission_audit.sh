#!/usr/bin/env bash
# Lists source comments and messages admitting that behavior was left unported.
# Token counts cannot see these: a file can pass depth_audit with whole branches
# falling into a no-op. Each hit is a lead. Clear it by porting the behavior, or
# by rewording it if the text describes something legitimate, such as a guest ABI stub.
# A hit whose text is legitimate and cannot be reworded (TypeScript's own message or
# identifier, id's data) is listed in tools/admission_audit_allow.tsv as
# `path <TAB> fixed text on the line <TAB> reason`; listed hits are not printed.
# Usage: tools/admission_audit.sh [path...]
RX='unresolved|not (yet )?(constructed|ported|implemented|modeled|modelled|composed|wired|landed)|until .{0,30}lands|once .{0,30}lands|no landed|no concrete|deferred to|left for|see unresolved|slice report|parent reconciliation|out of (this )?slice|in this slice|for now|simplif(y|ied)|stub|placeholder|TODO|FIXME'
ALLOW="$(dirname "$0")/admission_audit_allow.tsv"
grep -rnIEi "$RX" "${@:-src}" ${1:+} --include='*.c' --include='*.h' | grep -vE 'QA_CATALOG_UNRESOLVED|"[^"]*unresolved[^"]*"|catalog/' |
  awk -F'\t' -v allow="$ALLOW" '
    BEGIN {
      n = 0
      while ((getline row < allow) > 0) {
        if (row ~ /^#/ || row == "") continue
        split(row, f, "\t")
        if (f[3] == "") { print "admission_audit_allow.tsv: entry without a reason: " row > "/dev/stderr"; exit 2 }
        path[n] = f[1]; text[n] = f[2]; n++
      }
    }
    {
      colon = index($0, ":"); file = substr($0, 1, colon - 1)
      rest = substr($0, colon + 1); line = substr(rest, index(rest, ":") + 1)
      for (i = 0; i < n; i++) if (file == path[i] && index(line, text[i]) > 0) next
      print
    }'
