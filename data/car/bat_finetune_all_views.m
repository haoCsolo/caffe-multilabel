clear,close all,clc

rootfd ='~/work/data/classification_resize/';
rootfd_test = '~/work/data/classification_test_crop/';
listfd ='~/work/caffe/caffe-mmlab-mmlab_shared_buffer/data/car/';
dbfd='~/work/caffe_data/';

for i=1:1
    %prep data
    if exist([dbfd,'train_leveldb'],'dir')
        rmdir([dbfd,'train_leveldb'],'s');
    end
    if exist([dbfd,'test_leveldb'],'dir')
        rmdir([dbfd,'test_leveldb'],'s');
    end
    
    cmd= ['convert_imageset.bin ',rootfd,' ',listfd,'train_car',...
        ' ',dbfd,'train_leveldb 1'];
    cmd
    system(cmd);
    cmd =['convert_imageset.bin ',rootfd_test,' ',listfd,'test_car_crop',...
       ' ',dbfd,'test_leveldb 0'];
    system(cmd);
    %cmd =['convert_imageset.bin ',rootfd,' ',listfd,'test_part_',num2str(i),...
     %   ' ',dbfd,'test_part_leveldb 0'];

    %write solver
    solv_name = '../../examples/imagenet_ft_car/imagenet_finetune_overfeat_solver.bak';
    solv_new = '../../examples/imagenet_ft_car/imagenet_finetune_overfeat_solver.prototxt';
    f1=fopen(solv_name,'r');
    f2=fopen(solv_new,'w');

    for k=1:2
        line = fgetl(f1);
        fprintf(f2,'%s\n',line);
    end
    fprintf(f2,'test_iter: 34\n');
    fprintf(f2,'test_interval: 50\n');
    fprintf(f2,'base_lr: 0.001\n');
    for k=3:5
        line = fgetl(f1);
    end
    for k=6:7
        line = fgetl(f1);
        fprintf(f2,'%s\n',line);
    end
    

    fprintf(f2,'stepsize: 1000\n');
    fprintf(f2,'display: 20\n');
    fprintf(f2,'max_iter: 3000\n');
    fprintf(f2,'momentum: 0.9\n');
    fprintf(f2,'weight_decay:0.0005\n');
    fprintf(f2,'snapshot: 1000\n');

    fprintf(f2,'snapshot_prefix: ');
    fprintf(f2, '\"imagenet_finetune_car\"\n');
    fprintf(f2, 'solver_mode: GPU\ndevice_id:0\n');
    fclose(f1);
    fclose(f2);
    %train model

    cmd=['GLOG_logtostderr=1 finetune_net.bin ',...
        '../../examples/imagenet_ft_car/imagenet_finetune_overfeat_solver.prototxt ',...
        '../../examples/imagenet_ft_car/imagenet-overfeat_iter_860000'];
    system(cmd);

end
