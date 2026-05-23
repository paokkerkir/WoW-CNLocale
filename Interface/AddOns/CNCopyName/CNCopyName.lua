-- CNCopyName.lua
-- Ctrl+RightClick any player name to copy it to clipboard.
--
-- Surfaces:
--   • Chat hyperlinks (player: links in any chat frame)
--   • PlayerFrame / TargetFrame / TargetofTargetFrame
--   • Guild roster  (Social → Guild tab, player-status and guild-status views)
--   • Friends list  (Social → Friends tab)
--   • /who results  (Who List frame)
--
-- DLL priority: CNLocale.dll → WoWTranslate.dll → editbox fallback
--
-- /cncopy <name>  — debug: copy a literal string to test the DLL path

-- ============================================================================
-- CLIPBOARD WRITE
-- ============================================================================

local function CopyName(name)
    if not name or name == "" then return end

    if UnitXP then
        -- 1. CNLocale.dll built-in clipboard hook
        local ok, res = pcall(function() return UnitXP("CNLocale", "copy", name) end)
        if ok and res == "ok" then
            DEFAULT_CHAT_FRAME:AddMessage("|cFF00CCFF[CNCopy]|r Copied: " .. name)
            return
        end

        -- 2. WoWTranslate.dll clipboard hook
        ok, res = pcall(function() return UnitXP("WoWTranslate", "copy", name) end)
        if ok and res == "ok" then
            DEFAULT_CHAT_FRAME:AddMessage("|cFF00CCFF[CNCopy]|r Copied: " .. name)
            return
        end
    end

    -- 3. Editbox fallback — highlight so user can press Ctrl+C then Esc
    ChatFrameEditBox:Show()
    ChatFrameEditBox:SetText(name)
    ChatFrameEditBox:HighlightText()
    DEFAULT_CHAT_FRAME:AddMessage(
        "|cFFFFFF00[CNCopy]|r No copy DLL — press Ctrl+C then Esc.")
end

-- ============================================================================
-- HELPER: wrap a global button-click function.
-- WoW 1.12 XML calls these as  FuncName(arg1)  where arg1 = button type string.
-- We receive it as btn, check for Ctrl+RightButton, then pass btn through to
-- the original so normal left-click / right-click menus keep working.
-- ============================================================================

local function WrapGlobalClick(fnName, getName)
    local fn = getglobal(fnName)
    if type(fn) ~= "function" then return false end
    setglobal(fnName, function(btn)
        if btn == "RightButton" and IsControlKeyDown() then
            local ok, name = pcall(getName)
            if ok and name and name ~= "" then
                CopyName(name)
                return   -- suppress normal right-click action
            end
        end
        fn(btn)   -- pass through with correct button type
    end)
    return true
end

-- ============================================================================
-- GUILD ROSTER
-- Two views share one OnClick: FriendsFrameGuildStatusButton_OnClick.
--   Player-status view : GuildFrameButton1..13
--   Guild-status  view : GuildFrameGuildStatusButton1..13
-- Both have a "Name" child frame containing the member name.
-- Scroll frame: GuildListScrollFrame
-- ============================================================================

local g_guild_hooked = false

local function HookGuildRoster()
    if g_guild_hooked then return true end
    g_guild_hooked = WrapGlobalClick("FriendsFrameGuildStatusButton_OnClick", function()
        local nameFrame = getglobal(this:GetName() .. "Name")
        return nameFrame and nameFrame:GetText() or ""
    end)
    return g_guild_hooked
end

-- ============================================================================
-- FRIENDS LIST
-- Function: FriendsFrameFriendButton_OnClick
-- Buttons : FriendsFrameFriendButton1..10
-- Each button has SetID(friendIndex), so GetFriendInfo(this:GetID()) is exact.
-- Scroll frame: FriendsFrameFriendsScrollFrame
-- ============================================================================

local g_friends_hooked = false

local function HookFriendsList()
    if g_friends_hooked then return true end
    g_friends_hooked = WrapGlobalClick("FriendsFrameFriendButton_OnClick", function()
        return GetFriendInfo(this:GetID()) or ""
    end)
    return g_friends_hooked
end

-- ============================================================================
-- /WHO RESULTS
-- Function: FriendsFrameWhoButton_OnClick  (NOT WhoFrameButton_OnClick)
-- Buttons : WhoFrameButton1..17
-- Each button has a "Name" child already populated with the player name.
-- Scroll frame: WhoListScrollFrame
-- ============================================================================

local g_who_hooked = false

local function HookWhoList()
    if g_who_hooked then return true end
    g_who_hooked = WrapGlobalClick("FriendsFrameWhoButton_OnClick", function()
        local nameFrame = getglobal(this:GetName() .. "Name")
        return nameFrame and nameFrame:GetText() or ""
    end)
    return g_who_hooked
end

-- ============================================================================
-- UNIT FRAMES  (PlayerFrame, TargetFrame, TargetofTargetFrame)
-- All three already have "LeftButtonUp","RightButtonUp" registered.
-- ============================================================================

local function HookUnitFrames()
    WrapGlobalClick("PlayerFrame_OnClick", function()
        return UnitName("player") or ""
    end)
    WrapGlobalClick("TargetFrame_OnClick", function()
        return UnitName("target") or ""
    end)
    WrapGlobalClick("TargetofTarget_OnClick", function()
        return UnitName("targettarget") or ""
    end)
end

-- ============================================================================
-- CHAT HYPERLINK HOOK
-- Ctrl+RightClick a player: link in any chat frame.
-- ============================================================================

local function HookHyperlinks()
    local orig = ChatFrame_OnHyperlinkShow
    if not orig then return end
    ChatFrame_OnHyperlinkShow = function(link, text, button)
        if button == "RightButton" and IsControlKeyDown() then
            local _, _, playerName = string.find(link, "^player:(.+)")
            if playerName and playerName ~= "" then
                CopyName(playerName)
                return
            end
        end
        orig(link, text, button)
    end
end

-- ============================================================================
-- SLASH COMMAND  /cncopy <text>  — test the DLL clipboard path directly
-- ============================================================================

SLASH_CNCOPY1 = "/cncopy"
SlashCmdList["CNCOPY"] = function(msg)
    msg = msg and string.gsub(msg, "^%s+", "") or ""
    if msg == "" then
        DEFAULT_CHAT_FRAME:AddMessage(
            "|cFF00CCFFCNCopyName|r Usage: /cncopy <text>")
        return
    end
    CopyName(msg)
end

-- ============================================================================
-- INIT
-- ============================================================================

local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_LOGIN")
f:SetScript("OnEvent", function()
    -- ---- first-time setup ----
    if event == "PLAYER_LOGIN" then
        HookHyperlinks()
        HookUnitFrames()
        HookGuildRoster()
        HookFriendsList()
        HookWhoList()

        -- Belt-and-suspenders: if any list hook failed (function not yet
        -- defined), retry the first time that panel's data arrives.
        if not g_guild_hooked   then f:RegisterEvent("GUILD_ROSTER_UPDATE") end
        if not g_friends_hooked then f:RegisterEvent("FRIENDLIST_UPDATE") end
        if not g_who_hooked     then f:RegisterEvent("WHO_LIST_UPDATE") end

        DEFAULT_CHAT_FRAME:AddMessage(
            "|cFF00CCFFCNCopyName|r loaded — Ctrl+RightClick any name to copy."
            .. " [guild=" .. (g_guild_hooked   and "ok" or "lazy")
            .. " friends=" .. (g_friends_hooked and "ok" or "lazy")
            .. " who=" .. (g_who_hooked     and "ok" or "lazy") .. "]")
        return
    end

    -- ---- lazy-hook retries ----
    if event == "GUILD_ROSTER_UPDATE" and not g_guild_hooked then
        if HookGuildRoster() then f:UnregisterEvent("GUILD_ROSTER_UPDATE") end
        return
    end
    if event == "FRIENDLIST_UPDATE" and not g_friends_hooked then
        if HookFriendsList() then f:UnregisterEvent("FRIENDLIST_UPDATE") end
        return
    end
    if event == "WHO_LIST_UPDATE" and not g_who_hooked then
        if HookWhoList() then f:UnregisterEvent("WHO_LIST_UPDATE") end
        return
    end
end)
