function isBtSupported() {
    return (navigator.bluetooth) ? true : false;
}


function setStatus(newStatusHTML) {
    document.getElementById('status').innerHTML = `Status: ${newStatusHTML}`;
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
        acceptAllDevices : true
    };

    await navigator.bluetooth.requestDevice(options).then(
        (device) => {
            console.log(device);
        },
        (error) => {
            setStatus(`Cannot connect!`);
            console.log(error);
        }
    );
}


async function startup() {
    let result = await checkBtSupport();
    setConnectButtonState(!result);
}