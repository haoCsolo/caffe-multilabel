clear,close all,clc

datDir='~/work/data/classification_resize';
testDir='~/work/data/verification_resize';
load select_data2%s_id, s_v_id
load s_attr
load s_v_attr
rng(0);%rand seed
%s_id = s_v_id;%for verification
n=length(s_id);
n_v = length(s_v_id);
im_n = zeros(n,8);
train_filename='train_car_attr_disc';
test_filename = 'test_car_attr_disc';

test_crop = 20;
full_l=256;
crop_l=227;
modules = 5;
crop_s = full_l - crop_l + 1;
%256 -227
for i=1:modules
    f_train(i) = fopen([train_filename,'_',num2str(i)],'w');
    f_test(i) = fopen([test_filename,'_',num2str(i)],'w');
end
% half train, half test
%train dup images [5/im_n] times
% pos = randi(crop_s,n,2,test_crop);
rng(0);

%%%%%%% organize training data %%%%%%%%%%%%%%
for i=1:n
    for j=1:modules % 5 views
        im_list = dir([datDir,'/',num2str(s_id(i)),'/',num2str(j),'/*.jpg']);
        im_n(i,j) = length(im_list);      
                
        for k=1:im_n(i,j)
          
            fprintf(f_train(j),[num2str(s_id(i)),'/',num2str(j),'/',im_list(k).name]);
            fprintf(f_train(j),' %f %f %f\n',s_attr(i,3), s_attr(i,4),s_attr(i,5));
          
        end



    end
end
%%%%%%%%%%% organize testing data %%%%%%%%%%%%%%%
for i=1:n_v
    for j=1:modules % 5 views
        im_list = dir([testDir,'/',num2str(s_v_id(i)),'/',num2str(j),'/*.jpg']);
        im_n(i,j) = length(im_list);      
                
        for k=1:im_n(i,j)
          
            fprintf(f_test(j),[num2str(s_v_id(i)),'/',num2str(j),'/',im_list(k).name]);
            fprintf(f_test(j),' %f %f %f\n',s_v_attr(i,3), s_v_attr(i,4),s_v_attr(i,5));
          
        end
    end
end
for i=1:modules
    fclose(f_train(i));
    fclose(f_test(i));
end
%max(im_n,[],1)
