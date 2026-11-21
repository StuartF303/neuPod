# data

Implements the ports that `domain/ports/` will declare: the database, scanner, tag reader, art cache and folder
access. May import `domain/` and Flutter plugins (including `packages/neupod_storage` once it exists); never `ui/`,
`app/` or Riverpod. Empty until phase 1.
