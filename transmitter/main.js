var targetBTchar = null;
var targetBTserver = null;
var jobChain = null;

function isBtSupported() {
    return (navigator.bluetooth) ? true : false;
}


function setStatus(newStatusHTML) {
    document.getElementById('status').innerHTML = `Status: ${newStatusHTML}`;
}


function disconnectedBtns() {
    document.getElementById('disconnect').disabled = false;
    document.getElementById('connect').disabled = true;
}


function connectedBtns() {
    document.getElementById('disconnect').disabled = true;
    document.getElementById('connect').disabled = false;
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
        acceptAllDevices : true,
        optionalServices : ['000000ff-0000-1000-8000-00805f9b34fb']
    };

    await navigator.bluetooth.requestDevice(options).then(
        (device) => {
            console.log(device);
            device.gatt.connect().then((server) => {
                console.log(server);
                server.getPrimaryService('000000ff-0000-1000-8000-00805f9b34fb').then((service) => {
                    console.log(service);
                    service.getCharacteristics().then(chars => {
                        console.log(chars);
                        targetBTserver = server;
                        targetBTchar = chars[0];

                        connectedBtns();
                    });
                });
            });
        },
        (error) => {
            setStatus(`Cannot connect!`);
            console.log(error);
        }
    );
}


function disconnect() {
    if(targetBTserver != null) {
        targetBTserver.disconnect();

        disconnectedBtns();
    }
}


async function startup() {
    let result = await checkBtSupport();
    setConnectButtonState(!result);
}


function addWriteJob(...bufferToBeSended) {
    if(jobChain == null) {
        jobChain = new Promise((resolve) => {
            resolve("Start");
        });
    }
    
    jobChain = jobChain.then(
        () => targetBTchar.writeValueWithoutResponse(Uint8Array.of(...bufferToBeSended)).then(
            () => {
            },
            (error) => {
                console.log(error);
            },
        ),
        (error) => {
            console.log(error);
        },
    );

    /*
    jobChain = jobChain.then(() => {
        return new Promise((resolve) => {
            setTimeout(() => {
                resolve('Next')
                console.log('Next')}, 
            minIntervalMs);
        });
    });
    */
}


document.addEventListener("keydown", (event) => {
    if(targetBTchar != null && targetBTserver != null) {
        console.log("Adding to the queue: " + event.key);

        let charToBeSended = event.key.charCodeAt(0);

        addWriteJob(charToBeSended);
    }
    else {
        disconnectedBtns();
    }  
});