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

export const writeHeatpumpData = async (data) => {
  try {
    if (!data) {
      throw new Error('No data provided to writeHeatpumpData');
    }

    if(isNaN(data.temp)) data.temp = "20";
    
    await set(ref(db, '/heatpump/data'), data);
    console.log('Heatpump data successfully written');
    return true; // Indicate success
  } catch (error) {
    console.error('Error writing heatpump data:', error.message);
    return false; // Indicate failure
  }
};

export const listenForResponse = (cb) => {
  const responseRef = ref(db, `/response`);

  const unsubscribe = onValue(responseRef, (snapshot) => {
    if (snapshot.exists()) {
      cb(snapshot.val());
    } else {
      cb("No data available");
    }
  }, (error) => {
    console.error(error);
  });

  // Return an unsubscribe function so the caller can stop listening
  return unsubscribe;
};

export const listenForDeviceStatus = (callback) => {
  const statusRef = ref(db, "/status/heartbeat");

  return onValue(statusRef, (snapshot) => {
    if (snapshot.exists()) {
      callback(snapshot.val());
    } else {
      callback(null);
    }
  }, (error) => {
    console.error("Device status error:", error);
    callback(null);
  });
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