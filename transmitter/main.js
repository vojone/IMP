/**
 * BLE low energy transmitter for morse code ESP32 project
 * @author Vojtech Dvorak (xdvora3o)
 */

const deviceName = 'Morse code - receiver'; //Expected device name
const serviceId = '0000abcd-0000-1000-8000-00805f9b34fb'; //Our custom service UUID + base UUID

const minIntervalMs = 500;

var BTserver = null; //BluetoothRemoteGATTServer
var letterBTchar = null; //Characteristic of BTserver for writing letters
var volumeBTchar = null; //Characteristic of BTserver for reading current volume
var abortBTchar = null; //Characteristic of BTserver for aborting morse beeping
var jobChain = null; //Chain of promises for BTserver (to avoid sending request when server is busy)


function isBtSupported() {
    return (navigator.bluetooth) ? true : false;
}


function setStatusClass(appearanceClass) {
    document.getElementById('status').className = `status-bar ${appearanceClass}`;
}


function setStatus(newStatusHTML) {
    document.getElementById('status').innerHTML = `${newStatusHTML}`;
}


/**
 * Sets all buttons to the disconnected state
 */
function disconnectedBtns() {
    document.getElementById('disconnect').disabled = true;
    document.getElementById('volume').disabled = true;
    document.getElementById('connect').disabled = false;
    document.getElementById('abort').disabled = true;
    document.getElementById('send').disabled = true;
}


/**
 * Sets all buttons to the connected state
 */
function connectedBtns() {
    document.getElementById('disconnect').disabled = false;
    document.getElementById('volume').disabled = false;
    document.getElementById('connect').disabled = true;
    document.getElementById('abort').disabled = false;
    document.getElementById('send').disabled = false;
}


/**
 * 
 * @returns {Promise}
 */
function getBtAvailability() {
    return navigator.bluetooth.getAvailability();
}


/**
 * Returns browser name (for determining if brower supports web bluetooth or not)
 * @param {string} userAgentString 
 * @returns string
 */
function getBrowserName(userAgentString) {
    let browsers = ['Firefox', 'Chrome', 'Edg', 'Opera', 'Safari'];
    
    let curBrowser = null;
    for(let i = 0; i < browsers.length; i++) {
        if(userAgentString.includes(browsers[i] + '/')) {
            curBrowser = browsers[i];
            break;
        }
    }


    return `${curBrowser} v${userAgentString.split(curBrowser + '/')[1]}`;
}


/**
 * Checks the webbluetooth suppoert
 * @returns boolean
 */
async function checkBtSupport() {
    if(!isBtSupported()) {
        let userAgent = navigator.userAgent;
        setStatus(`Your browser (${getBrowserName(userAgent)}) probably does not support the WebBluetooth API!`);

        return false;
    }


    const btAvailable = getBtAvailability();

    return await btAvailable.then(
        (isAvailable) => { 
            if(!isAvailable) {
                setStatus('Bluetooth is not available on this device!');
            }

            return isAvailable;
        }
    );
}

/**
 * Sets the state of connect button
 * @param {boolean} newState 
 */
function setConnectButtonState(newState = false) {
    document.getElementById('connect').disabled = newState;
}

/**
 * Connects to the bluetooth device
 */
async function connect() {
    let options = {
        filters : [{ name: [deviceName] }], //Change to acceptAllDevices for all BT devices
        //acceptAllDevices : true,
        optionalServices : [serviceId]
    };

    setStatus('Connecting... 0%');

    await navigator.bluetooth.requestDevice(options).then(
        (device) => {
            console.log(device);
            setStatus('Connecting... 25%');
            setStatusClass('info');

            device.gatt.connect().then((server) => {
                console.log(server);
                setStatus('Connecting... 50%');

                server.getPrimaryService(0xabcd).then((service) => {
                    console.log(service);
                    setStatus('Connecting... 75%');

                    service.getCharacteristics().then(chars => {
                        console.log(chars);
                        setStatus('Connecting... 100%');

                        BTserver = server;
                        letterBTchar = chars[0];
                        volumeBTchar = chars[1];
                        abortBTchar = chars[2];

                        connectedBtns();
                        updateVolumeSlider();

                        setStatus('Connected!');
                        setStatusClass('success');
                    });
                });
            });
        },
        (error) => {
            setStatus('Error while conneting to the device!');
            setStatusClass('danger');

            console.log(error);
        }
    );
}

/**
 * Disconnects fro mthe bluetooth device
 */
function disconnect() {
    if(BTserver != null) {
        console.log(BTserver.connected);
        BTserver.disconnect();
        console.log(BTserver.connected);

        setStatus('Disconnected!');
        setStatusClass('warning');

        disconnectedBtns();

        jobChain = null;
        BTserver = null;
        letterBTchar = null;
        volumeBTchar = null;
        abortBTchar = null;
    }
}


/**
 * Startup function when body of the document is loaded
 */
async function startup() {
    let result = await checkBtSupport();
    setConnectButtonState(!result);

    setTypeMode();
    unsetBatchMode();
}


/**
 * Adds write job for the bluetooth device to job chain (chain of promises)
 * @param {object} char target characteristic of bluetooth device
 * @param {array} bufferToBeSended buffer to be sended to characteristic
 */
function addWriteJob(char, bufferToBeSended) {
    if(jobChain == null) {
        jobChain = new Promise((resolve) => {
            resolve('Start');
        });
    }
    
    jobChain = jobChain.then(
        () => char.writeValueWithoutResponse(Uint8Array.of(...bufferToBeSended)).catch((error) => {
            console.log(error);

            setStatus('Disconnected');
            setStatusClass('warning');

            disconnectedBtns();

            jobChain = null;
            BTserver = null;
            letterBTchar = null;
            volumeBTchar = null;
            abortBTchar = null;
        })
    );

    jobChain = jobChain.then(() => {
        return new Promise((resolve) => {
            setTimeout(resolve, minIntervalMs);
        });
    });
}

/**
 * Reads the volume from the characteristic
 */
function readVolume() {
    volumeBTchar.readValue().then(
    (value) => {
        console.log(value);
        console.log(value.getUint8(0));
    },
    (error) => {
        console.log(error);
    });
}

/**
 * Updates volume slider due to ESPs volume
 */
function updateVolumeSlider() {
    if(volumeBTchar != null && BTserver != null) {
        volumeBTchar.readValue().then(
        (value) => {
            document.getElementById('volume').value = value.getUint8(0);
        },
        (error) => {
            console.log(error);
        });
    }
    else {
        setStatus('Disconnected');
        setStatusClass('warning');

        disconnectedBtns();
    }
}

/**
 * Aborts the beeping of the message
 */
function abort() {
    if(abortBTchar != null && BTserver != null) {
        addWriteJob(abortBTchar, [1]);
    }
    else {
        setStatus('Disconnected');
        setStatusClass('warning');

        disconnectedBtns();
    }
}

/**
 * Sends the message to the ESP
 */
function send() {
    if(letterBTchar != null && BTserver != null) {
        let message = document.getElementById('message').value.trim();

        if(message) {
            console.log('Sending message to receiver...');

            let messageArr = message.split('').map(ch => ch.charCodeAt(0));
            addWriteJob(letterBTchar, messageArr);
        }
    }
    else {
        setStatus('Disconnected');
        setStatusClass('warning');

        disconnectedBtns();
    }
}


/**
 * Handler for change volume event (it is needed to send the value to the ESP characteristic)
 * @param {event} event 
 */
function volumeChanged(event) {
    if(volumeBTchar != null && BTserver != null) {
        addWriteJob(volumeBTchar, [event.target.value]);
    }
    else {
        setStatus('Disconnected');
        setStatusClass('warning');

        disconnectedBtns();
    }
}

/**
 * Handler for changing mode event
 * @param {*} event 
 */
function modeChanged(event) {
    if(event.target.value == 'type') {
        setTypeMode();
        unsetBatchMode();
    }
    else {
        unsetTypeMode();
        setBatchMode();
    }
}


/**
 * Resolves the keydown event
 * @param {event} event 
 */
function onKeyDown(event) {
    if(letterBTchar != null && BTserver != null) {
        console.log('Adding to the queue: ' + event.key);

        let charToBeSended = event.key.charCodeAt(0);

        console.log(charToBeSended);

        addWriteJob(letterBTchar, [charToBeSended]);
    }
    else {
        setStatus('Disconnected');
        setStatusClass('warning');

        disconnectedBtns();
    }
}

function setTypeMode() {
    document.getElementById('type').className = '';

    document.addEventListener('keydown', onKeyDown);
}

function unsetTypeMode() {
    document.getElementById('type').className = 'hidden';

    document.removeEventListener('keydown', onKeyDown);
}


function setBatchMode() {
    document.getElementById('batch').className = '';
}


function unsetBatchMode() {
    document.getElementById('batch').className = 'hidden';
}


