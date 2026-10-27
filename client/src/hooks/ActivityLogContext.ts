import { createContext, useContext } from 'react'
import type { ActivityLogApi } from './useActivityLogStore'

export const ActivityLogContext = createContext<ActivityLogApi | null>(null)

export function useActivityLog(): ActivityLogApi {
  const value = useContext(ActivityLogContext)
  if (!value) throw new Error('useActivityLog phải nằm trong <ActivityLogContext.Provider>')
  return value
}
