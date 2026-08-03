<script>
  import { onMount } from "svelte";
  import { login, listenForLogs } from '$lib/Firebase.js';
  import { writable } from 'svelte/store';
  import DeviceStatusCard from '../DeviceStatusCard.svelte';

  const LOGS_STORAGE_KEY = "IVT-logEntries";

  let userInfo = writable();
  let loading = true;
  let loginError = writable();
  let logEntries = [];
  let logsError = "";

  function readStoredLogs() {
    if (typeof window === "undefined") {
      return [];
    }

    try {
      const stored = localStorage.getItem(LOGS_STORAGE_KEY);
      return stored ? JSON.parse(stored) : [];
    } catch (error) {
      console.error("Failed to read stored logs:", error);
      return [];
    }
  }

  function persistLogs(entries) {
    if (typeof window === "undefined") {
      return;
    }

    try {
      localStorage.setItem(LOGS_STORAGE_KEY, JSON.stringify(entries));
    } catch (error) {
      console.error("Failed to persist logs:", error);
    }
  }

  function mergeLogEntries(incomingEntries = []) {
    const merged = [...logEntries];
    const seen = new Set(merged.map((entry) => `${entry.ts ?? ""}:${entry.tag ?? ""}:${entry.msg ?? ""}`));

    incomingEntries.forEach((entry) => {
      const key = `${entry?.ts ?? ""}:${entry?.tag ?? ""}:${entry?.msg ?? ""}`;
      if (!seen.has(key)) {
        merged.push(entry);
        seen.add(key);
      }
    });

    merged.sort((left, right) => Number(left.ts ?? 0) - Number(right.ts ?? 0));
    const trimmed = merged.slice(-200);
    logEntries = trimmed;
    persistLogs(trimmed);
  }

  async function initializePage() {
    try {
      logEntries = readStoredLogs();
      await login(loginError, userInfo);
    } catch (err) {
      console.error("Error initializing page:", err);
      loginError.set(err instanceof Error ? err.message : String(err));
    } finally {
      loading = false;
    }
  }

  onMount(() => {
    initializePage();

    const unsubscribeLogs = listenForLogs((entries, error) => {
      if (error) {
        logsError = error;
        return;
      }

      mergeLogEntries(entries || []);
      logsError = "";
    });

    return () => {
      unsubscribeLogs();
    };
  });

  function reload() {
    window.location.reload();
  }

  function getLogLevelBadgeClass(level) {
    const normalizedLevel = String(level ?? "INFO").toUpperCase();

    switch (normalizedLevel) {
      case "TRACE":
        return "badge-ghost";
      case "DEBUG":
        return "badge-neutral";
      case "INFO":
        return "badge-info";
      case "WARN":
        return "badge-warning";
      case "ERROR":
      case "FATAL":
        return "badge-error";
      default:
        return "badge-info";
    }
  }
</script>

<div class="flex justify-center w-full min-h-screen select-none bg-base-200">
  {#if loading}
    <div class="mt-24 loading">Loading...</div>
  {:else if $loginError}
    <h1 class="mt-10">Login failed. Please try again.</h1>
    <button class="mt-5 rounded-sm btn btn-secondary" on:click={reload}>Try Logging In Again</button>
    <p class="mt-10 text-red-500">{$loginError}</p>
  {:else if $userInfo}
    <div class="mt-10 mb-20 w-11/12 space-y-6">
      <DeviceStatusCard />

      <div class="rounded-box border border-base-300 bg-base-100 p-4 shadow-sm">
        <div class="mb-3 flex items-center justify-between gap-3">
          <div>
            <h2 class="text-xl font-semibold">Device logs</h2>
          </div>
          <span class="text-sm opacity-70">{logEntries.length} entries</span>
        </div>

        {#if logsError}
          <p class="mb-3 text-sm text-error">{logsError}</p>
        {/if}

        {#if logEntries.length === 0}
          <p class="text-sm opacity-70">No logs received yet.</p>
        {:else}
<div class="max-h-[32rem] overflow-auto space-y-2">
  {#each logEntries as entry}
    <div class="rounded-lg border border-base-300 bg-base-100 p-3 shadow-sm">
      
      <!-- Header -->
      <div class="mb-2 flex items-center justify-between gap-2">
        <div class="flex items-center gap-2">
          <span
            class={`badge badge-sm font-semibold ${getLogLevelBadgeClass(entry.lvl)}`}
          >
            {entry.lvl || "INFO"}
          </span>

          <span class="truncate text-xs font-mono opacity-70">
            {entry.tag || "Logger"}
          </span>
        </div>

        <span class="shrink-0 text-xs font-mono opacity-50">
          {entry.ts ?? "-"}
        </span>
      </div>

      <!-- Message -->
      <div class="break-words text-sm leading-relaxed">
        {entry.msg || ""}
      </div>

    </div>
  {/each}
</div>
        {/if}
      </div>
    </div>
  {/if}
</div>
