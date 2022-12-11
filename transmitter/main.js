const deviceName = 'Morse code - receiver';
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


function disconnectedBtns() {
    document.getElementById('disconnect').disabled = true;
    document.getElementById('volume').disabled = true;
    document.getElementById('connect').disabled = false;
}


function connectedBtns() {
    document.getElementById('disconnect').disabled = false;
    document.getElementById('volume').disabled = false;
    document.getElementById('connect').disabled = true;
}


/**
 * 
 * @returns {Promise}
 */
function getBtAvailability() {
    return navigator.bluetooth.getAvailability();
}


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


function setConnectButtonState(newState = false) {
    document.getElementById('connect').disabled = newState;
}


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


async function startup() {
    let result = await checkBtSupport();
    setConnectButtonState(!result);
}


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


document.addEventListener('keydown', (event) => {
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
});
