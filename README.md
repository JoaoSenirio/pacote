# Integração Arduino IDE

## Instalação de drivers
Em computadores com **sistema operacional Windows** faz-se necessário a instalação de drivers (não assinados digitalmente) referentes à libusb-win32 (v1.2.6.0). Entretanto, a instalação de drivers não assinados tornou-se mais difícil em versões mais recentes do sistema operacional em questão. Desse modo, utilize a ferramenta *Zadig driver installer* e siga os passos explicitados abaixo. 

A versão mais recente da ferramenta *Zadig driver installer* para download pode ser encontrada [aqui](https://zadig.akeo.ie/).

### Download 
![](/windows_driver/Download_image.png)

### Execução do Zadig 
1. Baixe o arquivo Micronucleus.zip [aqui](https://github.com/JoaoSenirio/pacote/blob/package_upgrade/windows_driver/micronucleus.zip?raw=true) e o descompacte
2. Vá em Device -> Load Preset Device e procure pelo arquivo baixado: Micronucleus.cfg
3. Na escolha do driver, determine a libusb-win32(v1.2.6.0)
4. Pronto, agora é só clicar em Install

![](/windows_driver/Zadig.png)
  
