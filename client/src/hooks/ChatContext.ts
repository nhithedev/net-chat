import { createContext, useContext } from 'react'
import type { ChatApi } from './useChatPrototype'

export const ChatContext = createContext<ChatApi | null>(null)

export function useChat(): ChatApi {
  const value = useContext(ChatContext)
  if (!value) throw new Error('useChat phải nằm trong <ChatContext.Provider>')
  return value
}
