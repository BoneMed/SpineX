import warnings
warnings.filterwarnings('ignore', category=UserWarning)

import torch
from nnunetv2.training.nnUNetTrainer.nnUNetTrainer import nnUNetTrainer
from nnunetv2.nets.HFENet import build_hfenet
from torch.optim import Adam
from nnunetv2.training.lr_scheduler.polylr import PolyLRScheduler
from torch import nn
from typing import Tuple, Union, List


class nnUNetTrainerHFENet(nnUNetTrainer):
    def __init__(
        self, 
        plans: dict, 
        configuration: str, 
        fold: int, 
        dataset_json: dict,
        device: torch.device = torch.device('cuda')
    ):
        super().__init__(plans, configuration, fold, dataset_json, device)
        
        self.initial_lr = 1e-2  

        
    @staticmethod
    def build_network_architecture(architecture_class_name: str,
                                   arch_init_kwargs: dict,
                                   arch_init_kwargs_req_import: Union[List[str], Tuple[str, ...]],
                                   num_input_channels: int,
                                   num_output_channels: int,
                                   enable_deep_supervision: bool = True) -> nn.Module:
        arch_kwargs = {
            'n_stages': 6,
            'features_per_stage': [32, 64, 128, 256, 320, 320],
            'conv_op': 'torch.nn.modules.conv.Conv3d',
            'kernel_sizes': [[3, 3, 3], [3, 3, 3], [3, 3, 3], [3, 3, 3], [3, 3, 3], [3, 3, 3]],
            'strides': arch_init_kwargs['strides'],
            'n_conv_per_stage': [2, 2, 2, 2, 2, 2],
            'n_conv_per_stage_decoder': [2, 2, 2, 2, 2],
        }

        model = build_hfenet(
            arch_kwargs,
            num_input_channels,
            num_output_channels,
            deep_supervision=enable_deep_supervision
            )
        
        return model
                                   

class nnUNetTrainerHFENet_500epochs(nnUNetTrainerHFENet):
    def __init__(self, plans: dict, configuration: str, fold: int, dataset_json: dict,
                 device: torch.device = torch.device('cuda')):
        """used for debugging plans etc"""
        super().__init__(plans, configuration, fold, dataset_json, device)
        self.num_epochs = 500
        

class nnUNetTrainerHFENet_2000epochs(nnUNetTrainerHFENet):
    def __init__(self, plans: dict, configuration: str, fold: int, dataset_json: dict,
                 device: torch.device = torch.device('cuda')):
        """used for debugging plans etc"""
        super().__init__(plans, configuration, fold, dataset_json, device)
        self.num_epochs = 2000
        

    

    
    
