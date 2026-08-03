import { initializeApp } from "firebase/app";
import {
  getAuth,
  signInWithEmailAndPassword,
  setPersistence,
  browserLocalPersistence,
  onAuthStateChanged,
  signOut
} from "firebase/auth"
import { getDatabase, ref, set, onValue } from "firebase/database";
import { goto } from "$app/navigation";


// TODO: Replace the following with your app's Firebase project configuration
// See: https://firebase.google.com/docs/web/learn-more#config-object
var firebaseConfig = {
  apiKey: "AIzaSyD0Jyu7jbx3oD1lQSa6xwD2YLSASQEJE24",
  authDomain: "ivt-heatpump.firebaseapp.com",
  // The value of `databaseURL` depends on the location of the database
  databaseURL: "https://ivt-heatpump-default-rtdb.europe-west1.firebasedatabase.app",
  projectId: "ivt-heatpump",
};

// Initialize Firebase
const app = initializeApp(firebaseConfig);
const auth = getAuth(app)

let authReady = false;
let authReadyPromise = null;
let authReadyResolver = null;

const ensureAuthReady = () => {
  if (authReady) {
    return Promise.resolve();
  }

  if (!authReadyPromise) {
    authReadyPromise = new Promise((resolve) => {
      authReadyResolver = resolve;
    });
  }

  return authReadyPromise;
};

onAuthStateChanged(auth, () => {
  authReady = true;
  if (authReadyResolver) {
    authReadyResolver();
    authReadyResolver = null;
  }
  authReadyPromise = null;
});

const initializeAuthPersistence = async () => {
  if (typeof window === "undefined") {
    return;
  }

  try {
    await setPersistence(auth, browserLocalPersistence);
  } catch (error) {
    console.error("Failed to enable Firebase auth persistence:", error);
  }
};

export const login = async (loginError, userInfo) => {
  try {
    const user = await ensureAuthenticated();
    userInfo.set(user);
    loginError.set(null);
    return user;
  } catch (error) {
    console.error(error)
    loginError.set("Kunde inte logga in");
    goto("/login");
    throw error;
  }
};

export const loginWithEmailPassword = async (email, password) => {
  await initializeAuthPersistence();
  const userCredential = await signInWithEmailAndPassword(auth, email, password);

  if (typeof window !== "undefined") {
    localStorage.removeItem("IVT-loginInfo");
  }

  return userCredential.user;
};

export const logout = async () => {
  try {
    await signOut(auth);
    if (typeof window !== "undefined") {
      localStorage.removeItem("IVT-loginInfo");
    }
  } catch (error) {
    console.error("Failed to sign out:", error);
  }
};

const db = getDatabase(app);

export const ensureAuthenticated = async () => {
  if (auth.currentUser) {
    return auth.currentUser;
  }

  await initializeAuthPersistence();
  await ensureAuthReady();

  if (auth.currentUser) {
    return auth.currentUser;
  }

  throw new Error("Ingen autentisering tillgänglig");
};

export const writeHeatpumpData = async (data) => {
  try {
    if (!data) {
      throw new Error('No data provided to writeHeatpumpData');
    }

    await ensureAuthenticated();

    if (isNaN(data.temp)) data.temp = "20";

    await set(ref(db, '/heatpump/data'), data);
    console.log('Heatpump data successfully written');
    return true;
  } catch (error) {
    console.error('Fel vid skrivning av kommandodata:', error.message);
    return false;
  }
};

const normalizeResponsePayload = (payload) => {
  if (payload == null) {
    return null;
  }

  if (typeof payload === "string") {
    return { status: "unknown", message: payload, id: null, raw: payload };
  }

  if (typeof payload === "object") {
    return {
      ...payload,
      status: payload.status || "unknown",
      message: payload.message || "",
      error: payload.error || null,
      id: payload.id ?? payload.commandId ?? payload.command_id ?? null,
    };
  }

  return { status: "unknown", message: String(payload), id: null, raw: payload };
};

export const listenForResponse = (cb) => {
  let unsubscribe = null;
  let active = true;

  const attachListener = async () => {
    try {
      await ensureAuthenticated();
      if (!active) return;

      const responseRef = ref(db, `/response`);
      unsubscribe = onValue(responseRef, (snapshot) => {
        if (!active) return;
        if (snapshot.exists()) {
          cb(normalizeResponsePayload(snapshot.val()));
        } else {
          cb(null);
        }
      }, (error) => {
        console.error(error);
        cb(null, error.message);
      });
    } catch (error) {
      console.error("Kunde inte prenumerera på svarsupdateringar:", error.message);
      cb(null, error.message);
    }
  };

  attachListener();

  return () => {
    active = false;
    if (unsubscribe) {
      unsubscribe();
    }
  };
};

export const listenForDeviceStatus = (callback) => {
  let unsubscribe = null;
  let active = true;

  const attachListener = async () => {
    try {
      await ensureAuthenticated();
      if (!active) return;

      const statusRef = ref(db, "/status/heartbeat");
      unsubscribe = onValue(statusRef, (snapshot) => {
        if (!active) return;
        if (!snapshot.exists()) {
          callback(null);
          return;
        }

        const value = snapshot.val();
        if (typeof value === "string") {
          try {
            callback(JSON.parse(value));
          } catch (error) {
            console.error("Failed to parse device status payload:", error);
            callback(null);
          }
          return;
        }

        callback(value);
      }, (error) => {
        console.error("Fel i enhetsstatus:", error);
        callback(null);
      });
    } catch (error) {
      console.error("Kunde inte prenumerera på enhetsstatus:", error.message);
      callback(null);
    }
  };

  attachListener();

  return () => {
    active = false;
    if (unsubscribe) {
      unsubscribe();
    }
  };
};

export const listenForLogs = (callback) => {
  let unsubscribe = null;
  let active = true;

  const attachListener = async () => {
    try {
      await ensureAuthenticated();
      if (!active) return;

      const logsRef = ref(db, "/status/logs");
      unsubscribe = onValue(logsRef, (snapshot) => {
        if (!active) return;

        if (!snapshot.exists()) {
          callback([]);
          return;
        }

        const value = snapshot.val();
        const rawLogs = value && typeof value === "object" ? value : {};
        const entries = Object.values(rawLogs)
          .filter((entry) => entry && typeof entry === "object")
          .map((entry) => ({
            ts: entry.ts ?? null,
            lvl: entry.lvl ?? "INFO",
            tag: entry.tag ?? "Logger",
            msg: entry.msg ?? ""
          }));


        callback(entries);
      }, (error) => {
        console.error("Fel i logglistning:", error);
        callback([], error.message);
      });
    } catch (error) {
      console.error("Kunde inte prenumerera på loggar:", error.message);
      callback([], error.message);
    }
  };

  attachListener();

  return () => {
    active = false;
    if (unsubscribe) {
      unsubscribe();
    }
  };
};

// Lyssna på temperaturdata
export const listenToTemperatureData = (callback) => {
  const dataRef = ref(db, "/temp");

  return onValue(dataRef, (snapshot) => {
    if (snapshot.exists()) {
      callback(snapshot.val());
    } else {
      callback({});
    }
  }, (error) => {
    console.error("Database error:", error);
  });
};