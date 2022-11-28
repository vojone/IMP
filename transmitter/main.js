const deviceName = 'Morse code - receiver';
const serviceId = '0000abcd-0000-1000-8000-00805f9b34fb'; //Our custom service UUID + base UUID

const minIntervalMs = 500;

var BTserver = null; //BluetoothRemoteGATTServer
var letterBTchar = null; //Characteristic of BTserver for writing letters
var volumeBTchar = null; //Characteristic of BTserver for reading current volume
var jobChain = null; //Chain of promises for BTserver (to avoid sending request when server is busy)

function isBtSupported() {
    return (navigator.bluetooth) ? true : false;
}


function setStatus(newStatusHTML) {
    document.getElementById('status').innerHTML = `Status: ${newStatusHTML}`;
}


function disconnectedBtns() {
    document.getElementById('disconnect').disabled = true;
    document.getElementById('connect').disabled = false;
}


function connectedBtns() {
    document.getElementById('disconnect').disabled = false;
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

    await navigator.bluetooth.requestDevice(options).then(
        (device) => {
            console.log(device);
            device.gatt.connect().then((server) => {
                console.log(server);
                server.getPrimaryService(0xabcd).then((service) => {
                    console.log(service);
                    service.getCharacteristics().then(chars => {
                        console.log(chars);
                        BTserver = server;
                        letterBTchar = chars[0];
                        volumeBTchar = chars[1];

                        connectedBtns();

                        setStatus('Connected!');
                    });
                });
            });
        },
        (error) => {
            setStatus('Cannot connect!');
            console.log(error);
        }
    );
}


function disconnect() {
    if(BTserver != null) {
        BTserver.disconnect();

        setStatus('Disconnected');
        disconnectedBtns();

        jobChain = null;
        BTserver = null;
        letterBTchar = null;
        volumeBTchar = null;
    }
}


async function startup() {
    let result = await checkBtSupport();
    setConnectButtonState(!result);
}


function addWriteJob(...bufferToBeSended) {
    if(jobChain == null) {
        jobChain = new Promise((resolve) => {
            resolve('Start');
        });
    }
    
    jobChain = jobChain.then(
        () => letterBTchar.writeValueWithoutResponse(Uint8Array.of(...bufferToBeSended)).catch((error) => {
            console.log(error);

            setStatus('Disconnected');
            disconnectedBtns();

            jobChain = null;
            BTserver = null;
            letterBTchar = null;
            volumeBTchar = null;
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


document.addEventListener('keydown', (event) => {
    if(letterBTchar != null && BTserver != null) {
        console.log('Adding to the queue: ' + event.key);

        let charToBeSended = event.key.charCodeAt(0);

        addWriteJob(charToBeSended);
    }
    else {
        disconnectedBtns();
    }  
});