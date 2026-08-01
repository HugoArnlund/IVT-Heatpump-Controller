import { initializeApp } from "firebase/app";
import { getAuth, signInWithEmailAndPassword } from "firebase/auth"
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

export const login = (loginError, userInfo) => {
  //localStorage.setItem("IVT-loginInfo", )
  let login = JSON.parse(localStorage.getItem("IVT-loginInfo"))

  if(login?.email && login?.password) {    
    signInWithEmailAndPassword(auth, login.email, login.password)
      .then((userCredential) => {
        // Signed in 
        userInfo.set(userCredential.user);
        console.log(userCredential);
        // ...
      })
      .catch((error) => {
        console.error(error)
        loginError.set("Could not login");
        goto("/login");
      });
  } else {
      loginError.set( "No login info saved to localstorage");
      goto("/login");
  }


}

const db = getDatabase(app);

const getStoredLogin = () => {
  if (typeof window === "undefined") {
    return null;
  }

  try {
    const stored = localStorage.getItem("IVT-loginInfo");
    return stored ? JSON.parse(stored) : null;
  } catch (error) {
    console.error("Failed to read stored Firebase login:", error);
    return null;
  }
};

export const ensureAuthenticated = async () => {
  if (auth.currentUser) {
    return auth.currentUser;
  }

  const login = getStoredLogin();
  if (login?.email && login?.password) {
    const userCredential = await signInWithEmailAndPassword(auth, login.email, login.password);
    return userCredential.user;
  }

  throw new Error("No login info saved to localstorage");
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
    console.error('Error writing heatpump data:', error.message);
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
      console.error("Unable to subscribe to response updates:", error.message);
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
        console.error("Device status error:", error);
        callback(null);
      });
    } catch (error) {
      console.error("Unable to subscribe to device status:", error.message);
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