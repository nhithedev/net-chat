export function addUnique<T>(list: T[], item: T, key: (value: T) => unknown): T[] {
  return list.some((value) => key(value) === key(item)) ? list : [...list, item]
}

export function removeBy<T>(list: T[], key: (value: T) => unknown, target: unknown): T[] {
  return list.filter((value) => key(value) !== target)
}

export function matchesQuery(query: string, ...fields: string[]): boolean {
  const needle = query.trim().toLowerCase()
  return !needle || fields.some((field) => field.toLowerCase().includes(needle))
}
