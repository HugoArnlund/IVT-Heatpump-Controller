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
            <p class="text-sm opacity-70">Recent entries are kept locally and updated as new Firebase log data arrives.</p>
          </div>
          <span class="text-sm opacity-70">{logEntries.length} entries</span>
        </div>

        {#if logsError}
          <p class="mb-3 text-sm text-error">{logsError}</p>
        {/if}

        {#if logEntries.length === 0}
          <p class="text-sm opacity-70">No logs received yet.</p>
        {:else}
<div class="max-h-[32rem] overflow-auto rounded-lg border border-base-300 bg-base-100 font-mono text-sm">
  {#each logEntries as entry}
    <div class="group flex gap-3 border-b border-base-300 px-3 py-2 last:border-none hover:bg-base-200/50">
      
      <!-- Level -->
      <div class="w-16 shrink-0">
        <span
          class={`badge badge-sm w-full justify-center font-semibold ${
            entry.lvl === 'ERROR' || entry.lvl === 'FATAL'
              ? 'badge-error'
              : entry.lvl === 'WARN'
              ? 'badge-warning'
              : entry.lvl === 'DEBUG'
              ? 'badge-neutral'
              : 'badge-info'
          }`}
        >
          {entry.lvl || "INFO"}
        </span>
      </div>

      <!-- Timestamp -->
      <span class="w-28 shrink-0 text-xs opacity-60">
        {entry.ts ?? "-"}
      </span>

      <!-- Tag -->
      <span class="w-32 shrink-0 truncate text-xs font-semibold opacity-70">
        {entry.tag || "Logger"}
      </span>

      <!-- Message -->
      <span class="min-w-0 flex-1 break-all text-base-content">
        {entry.msg || ""}
      </span>

    </div>
  {/each}
</div>
        {/if}
      </div>
    </div>
  {/if}
</div>
