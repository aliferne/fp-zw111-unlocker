from pyfingerprint.pyfingerprint import PyFingerprint


def main():
    f = PyFingerprint(port="/dev/ttyACM0")
    if f.verifyPassword() != True:
        raise ValueError("Can not check the sensor")

    print(f.getStorageCapacity())


if __name__ == "__main__":
    main()
