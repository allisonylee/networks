# Instructions for setting up VirtualBox environment (A0)
1. Download and install VirtualBox: [https://virtualbox.org](https://www.virtualbox.org/)
2. Download the ubuntu 22.04 ISO [here](https://releases.ubuntu.com/jammy/)
3. Create a new VirtualBox VM with the self guided install using the Ubuntu ISO. All of the default settings are fine, but make sure to set a user password. 
4. Once the VM is created, log in and install needed dependencies with `sudo apt-get install git gcc openssh-server openssh-client -y`
5. From there, clone the assignments repo with `git clone https://bitbucket.org/jhu-computer-networks-fall26/assignments-fall26/`

## Developing on the virtual machine using VSCode

Many IDEs like VSCode and Zed support remote development. To SSH to the VM, add a port forwarding rule to the network interface to forward traffic from your localhost to port 22 of the VM. TO do this, go to the VM settings->Network->Adapter 1, make sure it is a NAT, and then open "Port Forwarding". Click the green plus icon to add a new rule with protocol `TCP`, Host and Guest IP blank, Guest port 22, and Host port whichever port you want to use to ssh to the VM, such as 2222. You can then ssh to the VM using `ssh user@127.0.0.1:2222`. 

See this guide for more information: https://code.visualstudio.com/docs/remote/ssh

## Transferring your code from/to the virtual machine
You can set up a shared folder in the VM settings to move files back and forth between your machine and the VM.


## Using git to move your code from/to the virtual machine
Your VM should have internet access if installed correctly. You can use your own private git repository to move code from/to the virtual machine.

### FAQ
Q: Can I run my code on my own system instead of the virtual machine?

A: Depends on the assignment and your operating system! We have ensured that all the assignments can be run in this VM. Please refer to the individual assignment README files to see what environments are supported. As a summary:

Linux users: Except for Assignment 2, you can use your own system to develop and test!

MAC users: Except for Assignments 2 and 3, you can use your own system to develop and test!

Windows users: You can use WSL for all assignments except Assignment 2.
